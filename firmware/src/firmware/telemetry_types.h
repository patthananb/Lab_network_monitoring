#pragma once

#include <stddef.h>
#include <stdint.h>

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

struct WeatherReading {
    int status;
    int16_t temperatureRaw;
    uint16_t humidityRaw;
};

struct TelemetrySnapshot {
    WeatherReading weather;
    PowerReading power;
    int powerStatus;
    uint16_t pollCount;
};

struct SDM120Param {
    uint16_t regAddr;
    const char *name;
    const char *unit;
    float PowerReading::*field;
};

extern const SDM120Param SDM120_PARAMS[];
extern const size_t SDM120_PARAM_COUNT;
