/*
 * ===========================================================
 *  Waveshare ESP32-S3-Relay-6CH
 *  Modbus RTU Master (XY-MD02 + SDM120) + Modbus TCP Slave + MQTT Publisher
 * ===========================================================
 *
 * Architecture:
 *   [XY-MD02 @0x01] -----------RS485--> [ESP32-S3] --+-- MQTT  (weather JSON, QoS 1)
 *   [Eastron SDM120 @0x02] ----RS485-->  RTU Master   |
 *                                                   +-- TCP Slave :502 (Hregs)
 *                                                   |
 *                                                   +-- mDNS  <SLAVE_HOSTNAME>.local
 *
 * Outputs:
 *   - MQTT: existing weather JSON on sensors/esp32/data at QoS 1
 *   - Modbus TCP Holding Registers: weather + SDM120 power meter values
 *
 * MQTT JSON payload:
 *   {"temperature": 24.9, "humidity": 48.6, "status": 0, "poll_count": 142}
 * On weather RTU error only "status" (and "poll_count") are sent.
 * SDM120 MQTT/Influx/Grafana wiring is intentionally left for a later change.
 *
 * TCP Slave Holding Register Map (FC03, 0-based):
 *   HREG 0: Temperature (raw x 0.1 C, signed)
 *   HREG 1: Humidity    (raw x 0.1 %RH, unsigned)
 *   HREG 2: Weather status (0=OK, 1=timeout, 2=CRC err, 3=exception, 4=bad response)
 *   HREG 3: Poll count     (rolls over at 65535)
 *   HREG 4: Power meter status (same status codes)
 *   HREG 5..20: SDM120 floats as IEEE754 high/low words:
 *       voltage, current, active power, apparent power, reactive power,
 *       power factor, frequency, total active energy
 *
 * Libraries (Arduino IDE -> Manage Libraries):
 *   - "MQTT"           by Joel Gaehwiler (256dpi/arduino-mqtt)  -- QoS 1 publisher
 *   - "modbus-esp8266" by emelianov
 *
 * SECURITY: Modbus TCP and anonymous MQTT have no authentication.
 * Keep this device on a trusted LAN/VLAN.
 */

#include <WiFi.h>
#include <ESPmDNS.h>
#include <MQTT.h>
#include <ModbusIP_ESP8266.h>
#include <HardwareSerial.h>
#include <string.h>
#include "secrets.h"

/* --- MQTT --- */
#define CLIENT_ID       "esp32s3-weather-station"
#define TOPIC_DATA      "sensors/esp32/data"
#define MQTT_QOS        1
#define MQTT_BUF_SIZE   256

/* --- mDNS hostname (advertised as <name>.local) --- */
#define SLAVE_HOSTNAME  "esp32s3-weather"

/* --- RS485 Pins (Waveshare ESP32-S3-Relay-6CH) --- */
#define RS485_UART_NUM  1
#define RS485_TX        17
#define RS485_RX        18
#define RS485_BAUD      9600

/* Uncomment if your board needs manual DE/RE direction control: */
// #define DIR_PIN 4

/* --- XY-MD02 RTU Parameters --- */
#define XYMD02_ADDR     0x01
#define XYMD02_FC       0x04    /* FC04: Read Input Registers */
#define XYMD02_REG      0x0001
#define XYMD02_QTY      0x0002

/* --- Eastron SDM120 RTU Parameters --- */
#define SDM120_ADDR                  0x02
#define SDM120_FC                    0x04
#define SDM120_FLOAT_QTY             0x0002
#define SDM120_INTER_READ_DELAY_MS   10
#define SDM120_REG_VOLTAGE           0x0000
#define SDM120_REG_CURRENT           0x0006
#define SDM120_REG_ACTIVE_POWER      0x000C
#define SDM120_REG_APPARENT_POWER    0x0012
#define SDM120_REG_REACTIVE_POWER    0x0018
#define SDM120_REG_POWER_FACTOR      0x001E
#define SDM120_REG_FREQUENCY         0x0046
#define SDM120_REG_TOTAL_ENERGY      0x0156

/* --- TCP Slave Holding Registers (0-based) --- */
#define HREG_TEMP              0
#define HREG_HUMI              1
#define HREG_STATUS            2
#define HREG_POLL_CNT          3
#define HREG_POWER_STATUS      4
#define HREG_POWER_VOLTAGE_H   5
#define HREG_POWER_VOLTAGE_L   6
#define HREG_POWER_CURRENT_H   7
#define HREG_POWER_CURRENT_L   8
#define HREG_POWER_WATTS_H     9
#define HREG_POWER_WATTS_L     10
#define HREG_POWER_VA_H        11
#define HREG_POWER_VA_L        12
#define HREG_POWER_VAR_H       13
#define HREG_POWER_VAR_L       14
#define HREG_POWER_PF_H        15
#define HREG_POWER_PF_L        16
#define HREG_POWER_FREQ_H      17
#define HREG_POWER_FREQ_L      18
#define HREG_POWER_ENERGY_H    19
#define HREG_POWER_ENERGY_L    20

/* --- Shared RTU status codes --- */
#define MODBUS_OK              0
#define MODBUS_TIMEOUT         1
#define MODBUS_CRC_ERROR       2
#define MODBUS_EXCEPTION       3
#define MODBUS_BAD_RESPONSE    4

/* --- Timing --- */
#define POLL_INTERVAL_MS         2000
#define WIFI_RECONNECT_INTERVAL  5000
#define MQTT_RECONNECT_INTERVAL  5000

/* --- Objects --- */
HardwareSerial RS485(RS485_UART_NUM);
ModbusIP        mb;
WiFiClient      espClient;
MQTTClient      mqttClient(MQTT_BUF_SIZE);

struct PowerReading {
    float voltage;
    float current;
    float activePower;
    float apparentPower;
    float reactivePower;
    float powerFactor;
    float frequency;
    float totalActiveEnergy;
};

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

/* --- Shared Modbus RTU register reader, returns status code --- */
int readModbusRegisters(uint8_t slaveAddr, uint8_t functionCode,
                        uint16_t startReg, uint16_t quantity,
                        uint16_t *registers, size_t registerCapacity) {
    if (quantity == 0 || quantity > registerCapacity) {
        return MODBUS_BAD_RESPONSE;
    }

    const size_t responseLen = 3 + 2 * quantity + 2;
    if (responseLen > 64) {
        return MODBUS_BAD_RESPONSE;
    }

    uint8_t req[8];
    req[0] = slaveAddr;
    req[1] = functionCode;
    req[2] = (startReg >> 8) & 0xFF;
    req[3] = startReg & 0xFF;
    req[4] = (quantity >> 8) & 0xFF;
    req[5] = quantity & 0xFF;
    uint16_t crc = modbusCRC16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    while (RS485.available()) RS485.read();

    #ifdef DIR_PIN
    digitalWrite(DIR_PIN, HIGH);
    #endif
    RS485.write(req, 8);
    RS485.flush();
    #ifdef DIR_PIN
    digitalWrite(DIR_PIN, LOW);
    #endif
    delay(1);

    uint8_t resp[64] = {0};
    size_t rx = RS485.readBytes(resp, responseLen);

    if (rx < responseLen) {
        if (rx == 5 && resp[0] == slaveAddr && resp[1] == (functionCode | 0x80)) {
            uint16_t calcCRC = modbusCRC16(resp, 3);
            uint16_t recvCRC = (uint16_t)resp[3] | ((uint16_t)resp[4] << 8);
            return calcCRC == recvCRC ? MODBUS_EXCEPTION : MODBUS_CRC_ERROR;
        }
        return MODBUS_TIMEOUT;
    }

    uint16_t calcCRC = modbusCRC16(resp, rx - 2);
    uint16_t recvCRC = (uint16_t)resp[rx - 2] | ((uint16_t)resp[rx - 1] << 8);
    if (calcCRC != recvCRC) return MODBUS_CRC_ERROR;
    if (resp[0] != slaveAddr || resp[1] != functionCode || resp[2] != 2 * quantity) {
        return MODBUS_BAD_RESPONSE;
    }

    for (uint16_t i = 0; i < quantity; i++) {
        registers[i] = ((uint16_t)resp[3 + 2 * i] << 8) | resp[4 + 2 * i];
    }
    return MODBUS_OK;
}

/* --- Read XY-MD02 via RTU, returns status code --- */
int readXYMD02(int16_t *temp, uint16_t *humi) {
    uint16_t registers[XYMD02_QTY] = {0};
    int status = readModbusRegisters(XYMD02_ADDR, XYMD02_FC,
                                     XYMD02_REG, XYMD02_QTY,
                                     registers, XYMD02_QTY);
    if (status == MODBUS_OK) {
        *temp = (int16_t)registers[0];
        *humi = registers[1];
    }
    return status;
}

float floatFromWords(uint16_t highWord, uint16_t lowWord) {
    uint32_t raw = ((uint32_t)highWord << 16) | lowWord;
    float value = 0.0f;
    memcpy(&value, &raw, sizeof(value));
    return value;
}

uint32_t rawFromFloat(float value) {
    uint32_t raw = 0;
    memcpy(&raw, &value, sizeof(raw));
    return raw;
}

int readSDM120Float(uint16_t regAddr, float *value) {
    uint16_t registers[SDM120_FLOAT_QTY] = {0};
    int status = readModbusRegisters(SDM120_ADDR, SDM120_FC,
                                     regAddr, SDM120_FLOAT_QTY,
                                     registers, SDM120_FLOAT_QTY);
    if (status == MODBUS_OK) {
        *value = floatFromWords(registers[0], registers[1]);
    } else {
        *value = 0.0f;
    }
    return status;
}

/* --- Read power meter via RTU, returns status code --- */
int readPowerMeter(PowerReading *reading) {
    struct PowerParam {
        uint16_t regAddr;
        float *value;
    };

    PowerParam params[] = {
        {SDM120_REG_VOLTAGE,        &reading->voltage},
        {SDM120_REG_CURRENT,        &reading->current},
        {SDM120_REG_ACTIVE_POWER,   &reading->activePower},
        {SDM120_REG_APPARENT_POWER, &reading->apparentPower},
        {SDM120_REG_REACTIVE_POWER, &reading->reactivePower},
        {SDM120_REG_POWER_FACTOR,   &reading->powerFactor},
        {SDM120_REG_FREQUENCY,      &reading->frequency},
        {SDM120_REG_TOTAL_ENERGY,   &reading->totalActiveEnergy},
    };

    for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
        int status = readSDM120Float(params[i].regAddr, params[i].value);
        if (status != MODBUS_OK) {
            return status;
        }
        delay(SDM120_INTER_READ_DELAY_MS);
    }

    return MODBUS_OK;
}

void writeFloatHregs(uint16_t highReg, uint16_t lowReg, float value) {
    uint32_t raw = rawFromFloat(value);
    mb.Hreg(highReg, (uint16_t)(raw >> 16));
    mb.Hreg(lowReg, (uint16_t)(raw & 0xFFFF));
}

void writePowerHregs(int status, const PowerReading *reading) {
    mb.Hreg(HREG_POWER_STATUS, (uint16_t)status);
    if (status != MODBUS_OK) return;

    writeFloatHregs(HREG_POWER_VOLTAGE_H, HREG_POWER_VOLTAGE_L, reading->voltage);
    writeFloatHregs(HREG_POWER_CURRENT_H, HREG_POWER_CURRENT_L, reading->current);
    writeFloatHregs(HREG_POWER_WATTS_H, HREG_POWER_WATTS_L, reading->activePower);
    writeFloatHregs(HREG_POWER_VA_H, HREG_POWER_VA_L, reading->apparentPower);
    writeFloatHregs(HREG_POWER_VAR_H, HREG_POWER_VAR_L, reading->reactivePower);
    writeFloatHregs(HREG_POWER_PF_H, HREG_POWER_PF_L, reading->powerFactor);
    writeFloatHregs(HREG_POWER_FREQ_H, HREG_POWER_FREQ_L, reading->frequency);
    writeFloatHregs(HREG_POWER_ENERGY_H, HREG_POWER_ENERGY_L, reading->totalActiveEnergy);
}

void publishWeatherTelemetry(int weatherStatus, int16_t tempRaw,
                             uint16_t humiRaw, uint16_t currentPollCount) {
    if (!mqttClient.connected()) return;

    char payload[MQTT_BUF_SIZE];
    if (weatherStatus == MODBUS_OK) {
        snprintf(payload, sizeof(payload),
                 "{\"temperature\":%.1f,\"humidity\":%.1f,"
                 "\"status\":0,\"poll_count\":%u}",
                 tempRaw / 10.0, humiRaw / 10.0, currentPollCount);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"status\":%d,\"poll_count\":%u}",
                 weatherStatus, currentPollCount);
    }

    mqttClient.publish(TOPIC_DATA, payload, false, MQTT_QOS);
}

/* TCP client connect callback. Hook for future ACL/IP filtering. */
bool cbConn(IPAddress ip) {
    Serial.printf("[TCP] Client connected: %s\n", ip.toString().c_str());
    return true;
}

/* --- WiFi bring-up + mDNS (re-usable on reconnect) --- */
void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(SLAVE_HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("Connecting to WiFi '%s'", WIFI_SSID);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nWiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
        if (MDNS.begin(SLAVE_HOSTNAME)) {
            MDNS.addService("modbus", "tcp", 502);
            Serial.printf("mDNS: %s.local\n", SLAVE_HOSTNAME);
        } else {
            Serial.println("[WARN] mDNS start failed");
        }
    } else {
        Serial.println("\n[WARN] WiFi connect failed; will retry in loop()");
    }
}

/* Non-blocking: one attempt per call, so mb.task() isn't starved. */
void tryMqttReconnect() {
    if (WiFi.status() != WL_CONNECTED) return;
    Serial.print("[MQTT] Connecting...");
    if (mqttClient.connect(CLIENT_ID)) {
        Serial.println(" connected");
    } else {
        /* arduino-mqtt exposes lastError() (-ve) and returnCode() (CONNACK) */
        Serial.printf(" failed lastError=%d returnCode=%d\n",
                      mqttClient.lastError(), mqttClient.returnCode());
    }
}

/* --- Setup --- */
void setup() {
    Serial.begin(115200);
    //while (!Serial) delay(10);

    Serial.println();
    Serial.println("===========================================");
    Serial.println(" ESP32-S3 Weather Station");
    Serial.println(" RTU Master + TCP Slave (:502) + MQTT");
    Serial.println("===========================================");

    RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    RS485.setTimeout(30);
    #ifdef DIR_PIN
    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(DIR_PIN, LOW);
    #endif
    Serial.printf("RS485: UART%d TX=GPIO%d RX=GPIO%d %d baud\n",
                  RS485_UART_NUM, RS485_TX, RS485_RX, RS485_BAUD);

    connectWiFi();

    mqttClient.begin(MQTT_BROKER, 1883, espClient);

    mb.server();
    mb.onConnect(cbConn);
    mb.addHreg(HREG_TEMP,     0);
    mb.addHreg(HREG_HUMI,     0);
    mb.addHreg(HREG_STATUS,   0);
    mb.addHreg(HREG_POLL_CNT, 0);
    for (uint16_t hreg = HREG_POWER_STATUS; hreg <= HREG_POWER_ENERGY_L; hreg++) {
        mb.addHreg(hreg, 0);
    }

    Serial.println("TCP Slave listening on port 502");
    Serial.println("Hregs: HR0=Temp, HR1=Humi, HR2=Status, HR3=PollCnt, HR4..HR20=SDM120");
    Serial.printf("MQTT broker: %s (QoS %d)\n", MQTT_BROKER, MQTT_QOS);
    Serial.printf("Topic: %s  (JSON payload)\n\n", TOPIC_DATA);
}

/* --- Main Loop --- */
uint16_t      pollCount     = 0;
unsigned long lastPoll      = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastMqttTry   = 0;

void handleNetworkWatchdogs() {
    /* WiFi watchdog */
    if (millis() - lastWifiCheck >= WIFI_RECONNECT_INTERVAL) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Disconnected, reconnecting...");
            WiFi.disconnect();
            connectWiFi();
        }
    }

    /* MQTT watchdog (non-blocking) */
    if (!mqttClient.connected() &&
        millis() - lastMqttTry >= MQTT_RECONNECT_INTERVAL) {
        lastMqttTry = millis();
        tryMqttReconnect();
    }
}

void handleSensorPolling() {
    /* Poll RTU devices at interval */
    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();

        int16_t  tempRaw = 0;
        uint16_t humiRaw = 0;
        PowerReading powerReading = {0};

        int weatherStatus = readXYMD02(&tempRaw, &humiRaw);
        int powerStatus = readPowerMeter(&powerReading);

        if (weatherStatus == MODBUS_OK) {
            pollCount++;
            float tempC = tempRaw / 10.0;
            float humiP = humiRaw / 10.0;

            /* Modbus TCP holding registers */
            mb.Hreg(HREG_TEMP,     (uint16_t)tempRaw);
            mb.Hreg(HREG_HUMI,     humiRaw);
            mb.Hreg(HREG_STATUS,   0);
            mb.Hreg(HREG_POLL_CNT, pollCount);

            Serial.printf("XY-MD02: %.1fC  %.1f%%RH  [OK #%u]\n",
                          tempC, humiP, pollCount);
        } else {
            mb.Hreg(HREG_STATUS, (uint16_t)weatherStatus);
            mb.Hreg(HREG_POLL_CNT, pollCount);
            Serial.printf("XY-MD02: ERROR (status=%d)\n", weatherStatus);
        }

        writePowerHregs(powerStatus, &powerReading);
        if (powerStatus == MODBUS_OK) {
            Serial.printf("SDM120: %.1fV  %.3fA  %.1fW  %.1fVA  %.1fVAr  pf=%.3f  %.1fHz  %.3fkWh [OK]\n",
                          powerReading.voltage,
                          powerReading.current,
                          powerReading.activePower,
                          powerReading.apparentPower,
                          powerReading.reactivePower,
                          powerReading.powerFactor,
                          powerReading.frequency,
                          powerReading.totalActiveEnergy);
        } else {
            Serial.printf("SDM120: ERROR (status=%d)\n", powerStatus);
        }

        publishWeatherTelemetry(weatherStatus, tempRaw, humiRaw, pollCount);
    }
}

void loop() {
    mb.task();
    mqttClient.loop();
    handleNetworkWatchdogs();
    handleSensorPolling();
}
