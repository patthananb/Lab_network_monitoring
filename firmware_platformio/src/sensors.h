#pragma once

#include <Arduino.h>
#include "firmware_config.h"
#include "modbus_rtu.h"
#include "telemetry_types.h"

int readXYMD02(ModbusRtuClient &rtu, int16_t *temp, uint16_t *humi);
int readSDM120Float(ModbusRtuClient &rtu, uint16_t regAddr, float *value);
int readPowerMeter(ModbusRtuClient &rtu, PowerReading *reading);

WeatherReading readWeather(ModbusRtuClient &rtu);
int readPower(ModbusRtuClient &rtu, PowerReading *reading);

float weatherTemperatureC(const WeatherReading *reading);
float weatherHumidityPercent(const WeatherReading *reading);
uint32_t rawFromFloat(float value);

void printPowerMeterReading(const PowerReading *reading);
