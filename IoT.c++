#include <WiFi.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <MFRC522.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

#define SS_PIN 5
#define RST_PIN 21
#define BUZZER_PIN 2  
#define LED_VERDE 14  
#define LED_AMARILLO 12  
#define LED_ROJO 13  

const char* ssid = "Tecnologias";
const char* password = "123456789";
const char* serverUrl = "http://10.10.24.6:6000/api/aa/insert";

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;
WebServer server(80);
WebSocketsServer webSocket(81);

String ultimoUID = "";
String ultimoIDProgramado = "";

void testLED() {
    Serial.println(" Probando LED...");
    digitalWrite(LED_VERDE, HIGH);
    delay(2000);
    digitalWrite(LED_VERDE, LOW);
    Serial.println(" LED Prueba completada.");
}

void connectWiFi() {
    Serial.println("Conectando a WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\n Conectado con IP: " + WiFi.localIP().toString());
}

void turnOffAllLEDs() {
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_AMARILLO, LOW);
    digitalWrite(LED_ROJO, LOW);
}

void sendDataToAPI(String uid, String idProgramado) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(serverUrl);
        http.addHeader("Content-Type", "application/json");

        String payload = "{\"uid\":\"" + uid + "\",\"idProgramado\":\"" + idProgramado + "\"}";
        Serial.println(" Enviando: " + payload);

        int httpResponseCode = http.POST(payload);
        Serial.println(" Respuesta HTTP: " + String(httpResponseCode));

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println(" Respuesta completa: " + response);

            DynamicJsonDocument doc(1024);
            deserializeJson(doc, response);
            String estadoLED = doc["estadoLED"].as<String>();

            Serial.println(" Estado LED recibido: " + estadoLED);

            // Turn off all LEDs first
            turnOffAllLEDs();

            // Determine which LED to turn on
            if (estadoLED == "verde") {
                digitalWrite(LED_VERDE, HIGH);
                Serial.println("🟢 LED VERDE ENCENDIDO");
            } else if (estadoLED == "amarillo") {
                digitalWrite(LED_AMARILLO, HIGH);
                Serial.println("🟡 LED AMARILLO ENCENDIDO");
            } else if (estadoLED == "rojo") {
                digitalWrite(LED_ROJO, HIGH);
                Serial.println("🔴 LED ROJO ENCENDIDO");
            } else {
                Serial.println("⚠️ No se recibió un estado válido para el LED");
                digitalWrite(LED_VERDE, HIGH);  // Default to green
            }

            // Turn off the LED after 2 seconds
            delay(2000);
            turnOffAllLEDs();

        } else {
            Serial.println("❌ Error en la petición HTTP");
        }

        http.end();
    } else {
        Serial.println("❌ Error en conexión WiFi. No se enviaron datos.");
    }
}

void sendDataToClients() {
    String json = "{\"uid\":\"" + ultimoUID + "\", \"idProgramado\":\"" + ultimoIDProgramado + "\"}";
    webSocket.broadcastTXT(json);
}

void handleRoot() {
    String html = "<html><head><title>Lector RFID</title></head><body>";
    html += "<h2>Acerca una tarjeta RFID</h2>";
    html += "<p><strong>UID de la tarjeta:</strong> <span id='uid'>" + ultimoUID + "</span></p>";
    html += "<p><strong>ID programado:</strong> <span id='idProgramado'>" + ultimoIDProgramado + "</span></p>";
    html += "<script>";
    html += "var ws = new WebSocket('ws://' + location.hostname + ':81/');";
    html += "ws.onmessage = function(event) {";
    html += "var data = JSON.parse(event.data);";
    html += "document.getElementById('uid').innerText = data.uid;";
    html += "document.getElementById('idProgramado').innerText = data.idProgramado;";
    html += "};";
    html += "</script></body></html>";
    server.send(200, "text/html", html);
}

void setup() {
    Serial.begin(115200);
    SPI.begin();
    rfid.PCD_Init();
    connectWiFi();

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(LED_VERDE, OUTPUT);
    pinMode(LED_AMARILLO, OUTPUT);
    pinMode(LED_ROJO, OUTPUT);

    // Ensure all LEDs are off at startup
    turnOffAllLEDs();

    for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

    testLED();

    server.on("/", handleRoot);
    server.begin();
    webSocket.begin();
}

void loop() {
    server.handleClient();
    webSocket.loop();

    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        delay(300);
        return;
    }

    Serial.print("✅ Tarjeta detectada - UID: ");
    ultimoUID = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
        char hex[3];
        sprintf(hex, "%02X", rfid.uid.uidByte[i]);  
        ultimoUID += hex;
    }
    Serial.println(ultimoUID);

    byte block = 16;
    byte buffer[18];
    byte size = sizeof(buffer);

    if (rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(rfid.uid)) == MFRC522::STATUS_OK) {
        if (rfid.MIFARE_Read(block, buffer, &size) == MFRC522::STATUS_OK) {
            ultimoIDProgramado = "";
            for (byte i = 0; i < 16; i++) {
                if (buffer[i] >= 32 && buffer[i] <= 126) {
                    ultimoIDProgramado += (char)buffer[i];
                }
            }
        }
    }

    Serial.println("📌 ID programado en la tarjeta: " + ultimoIDProgramado);

    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);

    sendDataToAPI(ultimoUID, ultimoIDProgramado);
    sendDataToClients();

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
}
