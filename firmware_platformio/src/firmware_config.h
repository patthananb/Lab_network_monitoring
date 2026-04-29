#pragma once

#include <stdint.h>

/* --- MQTT --- */
#define CLIENT_ID       "esp32s3-weather-station"
#define TOPIC_DATA      "sensors/esp32/data"
#define MQTT_QOS        1
#define MQTT_BUF_SIZE   512

/* --- mDNS hostname (advertised as <name>.local) --- */
#define SLAVE_HOSTNAME  "esp32s3-weather"

/* --- RS485 Pins (Waveshare ESP32-S3-Relay-6CH) --- */
#define RS485_UART_NUM  1
#define RS485_TX        17
#define RS485_RX        18
#define RS485_BAUD      9600
#define RS485_RX_TIMEOUT_SYMBOLS 1

/* Uncomment if your board needs manual DE/RE direction control. */
// #define DIR_PIN 4

/* --- XY-MD02 RTU Parameters --- */
#define XYMD02_ADDR     0x01
#define XYMD02_FC       0x04
#define XYMD02_REG      0x0001
#define XYMD02_QTY      0x0002

/* --- Eastron SDM120 RTU Parameters --- */
#define SDM120_ADDR                  2
#define SDM120_FC                    0x04
#define SDM120_FLOAT_QTY             0x0002
#define MODBUS_POST_REQUEST_DELAY_MS 1
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
#define SENSOR_SEQUENCE_DELAY_MS 10
#define WIFI_RECONNECT_INTERVAL  5000
#define MQTT_RECONNECT_INTERVAL  5000
