#include "modbus_rtu.h"

ModbusRtuClient::ModbusRtuClient(HardwareSerial &serial) : serial_(serial) {}

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

int ModbusRtuClient::readRegisters(uint8_t slaveAddr, uint8_t functionCode,
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

    while (serial_.available()) serial_.read();

    #ifdef DIR_PIN
    digitalWrite(DIR_PIN, HIGH);
    #endif
    serial_.write(req, 8);
    serial_.flush();
    #ifdef DIR_PIN
    digitalWrite(DIR_PIN, LOW);
    #endif
    delay(MODBUS_POST_REQUEST_DELAY_MS);

    uint8_t resp[64] = {0};
    size_t rx = serial_.readBytes(resp, responseLen);

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
