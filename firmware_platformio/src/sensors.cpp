#include "sensors.h"

#include <string.h>

extern const SDM120Param SDM120_PARAMS[] = {
    {SDM120_REG_VOLTAGE,        "Voltage",             "V",   &PowerReading::voltage},
    {SDM120_REG_CURRENT,        "Current",             "A",   &PowerReading::current},
    {SDM120_REG_ACTIVE_POWER,   "Active Power",        "W",   &PowerReading::activePower},
    {SDM120_REG_APPARENT_POWER, "Apparent Power",      "VA",  &PowerReading::apparentPower},
    {SDM120_REG_REACTIVE_POWER, "Reactive Power",      "VAr", &PowerReading::reactivePower},
    {SDM120_REG_POWER_FACTOR,   "Power Factor",        "-",   &PowerReading::powerFactor},
    {SDM120_REG_FREQUENCY,      "Frequency",           "Hz",  &PowerReading::frequency},
    {SDM120_REG_TOTAL_ENERGY,   "Total Active Energy", "kWh", &PowerReading::totalActiveEnergy},
};
extern const size_t SDM120_PARAM_COUNT = sizeof(SDM120_PARAMS) / sizeof(SDM120_PARAMS[0]);

static float *powerField(PowerReading *reading, const SDM120Param *param) {
    return &(reading->*(param->field));
}

static float powerFieldValue(const PowerReading *reading, const SDM120Param *param) {
    return reading->*(param->field);
}

static float floatFromWords(uint16_t highWord, uint16_t lowWord) {
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

int readXYMD02(ModbusRtuClient &rtu, int16_t *temp, uint16_t *humi) {
    uint16_t registers[XYMD02_QTY] = {0};
    int status = rtu.readRegisters(XYMD02_ADDR, XYMD02_FC,
                                   XYMD02_REG, XYMD02_QTY,
                                   registers, XYMD02_QTY);
    if (status == MODBUS_OK) {
        *temp = (int16_t)registers[0];
        *humi = registers[1];
    }
    return status;
}

int readSDM120Float(ModbusRtuClient &rtu, uint16_t regAddr, float *value) {
    uint16_t registers[SDM120_FLOAT_QTY] = {0};
    int status = rtu.readRegisters(SDM120_ADDR, SDM120_FC,
                                   regAddr, SDM120_FLOAT_QTY,
                                   registers, SDM120_FLOAT_QTY);
    if (status == MODBUS_OK) {
        *value = floatFromWords(registers[0], registers[1]);
    } else {
        *value = 0.0f;
    }
    return status;
}

int readPowerMeter(ModbusRtuClient &rtu, PowerReading *reading) {
    for (size_t i = 0; i < SDM120_PARAM_COUNT; i++) {
        int status = readSDM120Float(rtu, SDM120_PARAMS[i].regAddr,
                                     powerField(reading, &SDM120_PARAMS[i]));
        if (status != MODBUS_OK) {
            return status;
        }
        delay(SDM120_INTER_READ_DELAY_MS);
    }

    return MODBUS_OK;
}

WeatherReading readWeather(ModbusRtuClient &rtu) {
    WeatherReading reading = {MODBUS_TIMEOUT, 0, 0};
    reading.status = readXYMD02(rtu, &reading.temperatureRaw, &reading.humidityRaw);
    return reading;
}

int readPower(ModbusRtuClient &rtu, PowerReading *reading) {
    *reading = {};
    return readPowerMeter(rtu, reading);
}

float weatherTemperatureC(const WeatherReading *reading) {
    return reading->temperatureRaw / 10.0f;
}

float weatherHumidityPercent(const WeatherReading *reading) {
    return reading->humidityRaw / 10.0f;
}

void printPowerMeterReading(const PowerReading *reading) {
    for (size_t i = 0; i < SDM120_PARAM_COUNT; i++) {
        Serial.printf("0x%04X: %20s, %7.3f [%s]\n",
                      SDM120_PARAMS[i].regAddr,
                      SDM120_PARAMS[i].name,
                      powerFieldValue(reading, &SDM120_PARAMS[i]),
                      SDM120_PARAMS[i].unit);
    }
}
