/*
 * ===========================================================
 *  Waveshare ESP32-S3-Relay-6CH
 *  Modbus RTU Master (XY-MD02) + MQTT Client
 * ===========================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>
#include "secrets.h"

/* --- Configuration --- */
#define CLIENT_ID       "esp32s3-weather-station"
#define TOPIC_TEMP      "sensors/esp32/temperature"
#define TOPIC_HUMI      "sensors/esp32/humidity"
#define TOPIC_STATUS    "sensors/esp32/status"

/* --- RS485 Pins (Waveshare ESP32-S3-Relay-6CH) --- */
#define RS485_UART_NUM  1
#define RS485_TX        17
#define RS485_RX        18
#define RS485_BAUD      9600

/* --- XY-MD02 RTU Parameters --- */
#define XYMD02_ADDR     0x01
#define XYMD02_FC       0x04
#define XYMD02_REG      0x0001
#define XYMD02_QTY      0x0002
#define RTU_RESP_LEN    (3 + 2 * XYMD02_QTY + 2)

/* --- Timing --- */
#define POLL_INTERVAL_MS 5000

/* --- Objects --- */
HardwareSerial RS485(RS485_UART_NUM);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

/* --- RTU CRC-16 --- */
uint16_t modbusCRC16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

int readXYMD02(int16_t *temp, uint16_t *humi) {
    uint8_t req[8];
    req[0] = XYMD02_ADDR;
    req[1] = XYMD02_FC;
    req[2] = (XYMD02_REG >> 8) & 0xFF;
    req[3] = XYMD02_REG & 0xFF;
    req[4] = (XYMD02_QTY >> 8) & 0xFF;
    req[5] = XYMD02_QTY & 0xFF;
    uint16_t crc = modbusCRC16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    while (RS485.available()) RS485.read();
    RS485.write(req, 8);
    RS485.flush();

    uint8_t resp[RTU_RESP_LEN] = {0};
    size_t rx = RS485.readBytes(resp, RTU_RESP_LEN);

    if (rx < RTU_RESP_LEN) return 1;
    if (resp[1] & 0x80) return 3;
    uint16_t calcCRC = modbusCRC16(resp, rx - 2);
    uint16_t recvCRC = (uint16_t)resp[rx - 2] | ((uint16_t)resp[rx - 1] << 8);
    if (calcCRC != recvCRC) return 2;

    *temp = (int16_t)(((uint16_t)resp[3] << 8) | resp[4]);
    *humi = ((uint16_t)resp[5] << 8) | resp[6];
    return 0;
}

void setupWiFi() {
    delay(10);
    Serial.printf("\nConnecting to %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void reconnectMQTT() {
    while (!mqttClient.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (mqttClient.connect(CLIENT_ID)) {
            Serial.println("connected");
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    RS485.setTimeout(30);

    setupWiFi();
    mqttClient.setServer(MQTT_BROKER, 1883);
}

unsigned long lastPoll = 0;

void loop() {
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();

    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();

        int16_t tempRaw = 0;
        uint16_t humiRaw = 0;
        int status = readXYMD02(&tempRaw, &humiRaw);

        if (status == 0) {
            float tempC = tempRaw / 10.0;
            float humiP = humiRaw / 10.0;

            mqttClient.publish(TOPIC_TEMP, String(tempC).c_str());
            mqttClient.publish(TOPIC_HUMI, String(humiP).c_str());
            mqttClient.publish(TOPIC_STATUS, "0");

            Serial.printf("Published: %.1fC, %.1f%%RH\n", tempC, humiP);
        } else {
            mqttClient.publish(TOPIC_STATUS, String(status).c_str());
            Serial.printf("Sensor Error: %d\n", status);
        }
    }
}
