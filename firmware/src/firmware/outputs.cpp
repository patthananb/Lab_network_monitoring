#include "outputs.h"

#include <stdarg.h>
#include "firmware_config.h"
#include "sensors.h"

static void writeFloatHregs(ModbusIP &mb, uint16_t highReg, uint16_t lowReg, float value) {
    uint32_t raw = rawFromFloat(value);
    mb.Hreg(highReg, (uint16_t)(raw >> 16));
    mb.Hreg(lowReg, (uint16_t)(raw & 0xFFFF));
}

void writeWeatherHregs(ModbusIP &mb, const WeatherReading *reading, uint16_t currentPollCount) {
    if (reading->status == MODBUS_OK) {
        mb.Hreg(HREG_TEMP,     (uint16_t)reading->temperatureRaw);
        mb.Hreg(HREG_HUMI,     reading->humidityRaw);
        mb.Hreg(HREG_STATUS,   0);
    } else {
        mb.Hreg(HREG_STATUS, (uint16_t)reading->status);
    }

    mb.Hreg(HREG_POLL_CNT, currentPollCount);
}

void writePowerHregs(ModbusIP &mb, int status, const PowerReading *reading) {
    mb.Hreg(HREG_POWER_STATUS, (uint16_t)status);
    if (status != MODBUS_OK) return;

    writeFloatHregs(mb, HREG_POWER_VOLTAGE_H, HREG_POWER_VOLTAGE_L, reading->voltage);
    writeFloatHregs(mb, HREG_POWER_CURRENT_H, HREG_POWER_CURRENT_L, reading->current);
    writeFloatHregs(mb, HREG_POWER_WATTS_H, HREG_POWER_WATTS_L, reading->activePower);
    writeFloatHregs(mb, HREG_POWER_VA_H, HREG_POWER_VA_L, reading->apparentPower);
    writeFloatHregs(mb, HREG_POWER_VAR_H, HREG_POWER_VAR_L, reading->reactivePower);
    writeFloatHregs(mb, HREG_POWER_PF_H, HREG_POWER_PF_L, reading->powerFactor);
    writeFloatHregs(mb, HREG_POWER_FREQ_H, HREG_POWER_FREQ_L, reading->frequency);
    writeFloatHregs(mb, HREG_POWER_ENERGY_H, HREG_POWER_ENERGY_L, reading->totalActiveEnergy);
}

static void appendPayload(char *payload, size_t payloadSize, size_t *len, const char *format, ...) {
    if (*len >= payloadSize) return;

    va_list args;
    va_start(args, format);
    int written = vsnprintf(payload + *len, payloadSize - *len, format, args);
    va_end(args);

    if (written < 0) return;

    size_t remaining = payloadSize - *len;
    if ((size_t)written >= remaining) {
        *len = payloadSize - 1;
    } else {
        *len += (size_t)written;
    }
}

bool publishTelemetry(MQTTClient &mqttClient, const TelemetrySnapshot *snapshot) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Publish skipped: not connected");
        return false;
    }

    char payload[MQTT_BUF_SIZE];
    size_t len = 0;
    appendPayload(payload, sizeof(payload), &len,
                  "{\"status\":%d,\"poll_count\":%u",
                  snapshot->weather.status, snapshot->pollCount);

    if (snapshot->weather.status == MODBUS_OK) {
        appendPayload(payload, sizeof(payload), &len,
                      ",\"temperature_raw\":%d,\"humidity_raw\":%u,"
                      "\"temperature\":%.1f,\"humidity\":%.1f",
                      snapshot->weather.temperatureRaw,
                      snapshot->weather.humidityRaw,
                      weatherTemperatureC(&snapshot->weather),
                      weatherHumidityPercent(&snapshot->weather));
    }

    appendPayload(payload, sizeof(payload), &len,
                  ",\"power_status\":%d", snapshot->powerStatus);

    if (snapshot->powerStatus == MODBUS_OK) {
        appendPayload(payload, sizeof(payload), &len,
                      ",\"power_voltage\":%.1f,\"power_current\":%.3f,"
                      "\"power_watts\":%.1f,\"power_apparent_va\":%.1f,"
                      "\"power_reactive_var\":%.1f,\"power_factor\":%.3f,"
                      "\"power_frequency\":%.1f,\"power_energy_kwh\":%.3f",
                      snapshot->power.voltage,
                      snapshot->power.current,
                      snapshot->power.activePower,
                      snapshot->power.apparentPower,
                      snapshot->power.reactivePower,
                      snapshot->power.powerFactor,
                      snapshot->power.frequency,
                      snapshot->power.totalActiveEnergy);
    }

    appendPayload(payload, sizeof(payload), &len, "}");
    bool published = mqttClient.publish(TOPIC_DATA, payload, false, MQTT_QOS);
    Serial.printf("[MQTT] %s %s\n", published ? "Published" : "Publish failed", payload);
    return published;
}
