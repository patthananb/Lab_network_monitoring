#pragma once

#include <MQTT.h>
#include <ModbusIP_ESP8266.h>
#include "telemetry_types.h"

void writeWeatherHregs(ModbusIP &mb, const WeatherReading *reading, uint16_t currentPollCount);
void writePowerHregs(ModbusIP &mb, int status, const PowerReading *reading);

bool publishTelemetry(MQTTClient &mqttClient, const TelemetrySnapshot *snapshot);
