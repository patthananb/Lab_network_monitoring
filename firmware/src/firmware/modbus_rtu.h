#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "firmware_config.h"

uint16_t modbusCRC16(const uint8_t *data, size_t len);

class ModbusRtuClient {
public:
    explicit ModbusRtuClient(HardwareSerial &serial);

    int readRegisters(uint8_t slaveAddr, uint8_t functionCode,
                      uint16_t startReg, uint16_t quantity,
                      uint16_t *registers, size_t registerCapacity);

private:
    HardwareSerial &serial_;
};
