/*
 * ===========================================================
 *  Waveshare ESP32-S3-Relay-6CH
 *  Modbus RTU Master (XY-MD02 + SDM120) + Modbus TCP Slave + MQTT Publisher
 * ===========================================================
 *
 * Architecture:
 *   [XY-MD02 @0x01] -----------RS485--> [ESP32-S3] --+-- MQTT  (weather JSON, QoS 1)
 *   [Eastron SDM120 @slave 2] --RS485-->  RTU Master   |
 *                                                   +-- TCP Slave :502 (Hregs)
 *                                                   |
 *                                                   +-- mDNS  <SLAVE_HOSTNAME>.local
 *
 * Outputs:
 *   - MQTT: weather + SDM120 JSON on sensors/esp32/data at QoS 1
 *   - Modbus TCP Holding Registers: weather + SDM120 power meter values
 *
 * MQTT JSON payload:
 *   {"status":0,"poll_count":142,"temperature_raw":249,"humidity_raw":486,
 *    "temperature":24.9,"humidity":48.6,
 *    "power_status":0,"power_voltage":229.4,"power_current":0.418,
 *    "power_watts":74.6,"power_apparent_va":77.5,"power_reactive_var":12.0,
 *    "power_factor":0.96,"power_frequency":50.0,"power_energy_kwh":12.348}
 * On RTU errors, value fields for the failing device are omitted.
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

#include "firmware_config.h"
#include "modbus_rtu.h"
#include "outputs.h"
#include "secrets.h"
#include "sensors.h"
#include "telemetry_types.h"

HardwareSerial  RS485(RS485_UART_NUM);
ModbusRtuClient rtu(RS485);
ModbusIP        mb;
WiFiClient      espClient;
MQTTClient      mqttClient(MQTT_BUF_SIZE);

uint16_t      pollCount     = 0;
unsigned long lastPoll      = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastMqttTry   = 0;

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
        Serial.printf(" failed lastError=%d returnCode=%d\n",
                      mqttClient.lastError(), mqttClient.returnCode());
    }
}

void handleNetworkWatchdogs() {
    if (millis() - lastWifiCheck >= WIFI_RECONNECT_INTERVAL) {
        lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Disconnected, reconnecting...");
            WiFi.disconnect();
            connectWiFi();
        }
    }

    if (!mqttClient.connected() &&
        millis() - lastMqttTry >= MQTT_RECONNECT_INTERVAL) {
        lastMqttTry = millis();
        tryMqttReconnect();
    }
}

void handleWeatherReading(const WeatherReading *reading) {
    if (reading->status == MODBUS_OK) {
        pollCount++;
        writeWeatherHregs(mb, reading, pollCount);

        Serial.printf("XY-MD02: raw_temp=%d raw_humi=%u %.1fC %.1f%%RH [OK #%u]\n",
                      reading->temperatureRaw,
                      reading->humidityRaw,
                      weatherTemperatureC(reading),
                      weatherHumidityPercent(reading),
                      pollCount);
    } else {
        writeWeatherHregs(mb, reading, pollCount);
        Serial.printf("XY-MD02: ERROR (status=%d)\n", reading->status);
    }
}

void handlePowerReading(int status, const PowerReading *reading) {
    writePowerHregs(mb, status, reading);
    if (status == MODBUS_OK) {
        Serial.println("SDM120: [OK]");
        printPowerMeterReading(reading);
    } else {
        Serial.printf("SDM120: ERROR (status=%d)\n", status);
    }
}

void handleSensorPolling() {
    if (millis() - lastPoll < POLL_INTERVAL_MS) return;
    lastPoll = millis();

    TelemetrySnapshot snapshot = {};
    snapshot.weather = readWeather(rtu);
    handleWeatherReading(&snapshot.weather);
    delay(SENSOR_SEQUENCE_DELAY_MS);
    snapshot.powerStatus = readPower(rtu, &snapshot.power);
    handlePowerReading(snapshot.powerStatus, &snapshot.power);
    snapshot.pollCount = pollCount;

    publishTelemetry(mqttClient, &snapshot);
}

void setup() {
    Serial.begin(115200);

    Serial.println();
    Serial.println("===========================================");
    Serial.println(" ESP32-S3 Weather Station");
    Serial.println(" RTU Master + TCP Slave (:502) + MQTT");
    Serial.println("===========================================");

    RS485.begin(RS485_BAUD, SERIAL_8N1, RS485_RX, RS485_TX);
    RS485.setTimeout(30);
    RS485.setRxTimeout(RS485_RX_TIMEOUT_SYMBOLS);
    RS485.flush();
    #ifdef DIR_PIN
    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(DIR_PIN, LOW);
    #endif
    Serial.printf("RS485: UART%d TX=GPIO%d RX=GPIO%d %d baud\n",
                  RS485_UART_NUM, RS485_TX, RS485_RX, RS485_BAUD);
    Serial.printf("SDM120 power meter: Modbus RTU slave address %u\n", SDM120_ADDR);

    connectWiFi();

    mqttClient.begin(MQTT_BROKER, 1883, espClient);
    tryMqttReconnect();

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

void loop() {
    mb.task();
    mqttClient.loop();
    handleNetworkWatchdogs();
    handleSensorPolling();
}
