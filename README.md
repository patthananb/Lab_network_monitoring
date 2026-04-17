# ESP32-S3 Weather Station (TIG Stack + MQTT)

This project implements a weather monitoring system using an ESP32-S3, an XY-MD02 Modbus RTU sensor, and a TIG (Telegraf, InfluxDB, Grafana) stack for data visualization, with MQTT as the communication bridge.

## Architecture
- **Sensor**: XY-MD02 (Temperature & Humidity) via RS485 (Modbus RTU).
- **Edge Device**: ESP32-S3 (Waveshare Relay Board) polling the sensor and publishing to MQTT.
- **Broker**: Eclipse Mosquitto (MQTT).
- **Collector**: Telegraf (Subscribes to MQTT topics and writes to InfluxDB).
- **Storage**: InfluxDB v2.
- **Visualization**: Grafana.

## Hardware Requirements
- **Waveshare ESP32-S3-Relay-6CH** (or any ESP32-S3 with RS485 transceiver).
- **XY-MD02** Temperature & Humidity Sensor.
- **Raspberry Pi** (Production) or **PC with Docker** (Development).

## Software Requirements
- **Arduino IDE** with ESP32 Board Support.
- **Libraries**:
  - `PubSubClient` by Nick O'Leary.
- **Docker & Docker Compose**.

## Project Structure
```text
.
├── docker/
│   ├── docker-compose.yml   # TIG + MQTT Stack
│   ├── mosquitto.conf      # MQTT Broker Config
│   └── telegraf.conf       # Data Collection Config
└── firmware/
    └── src/
        ├── firmware.ino    # ESP32 Source Code
        └── secrets.h       # WiFi & MQTT Credentials
```

## Getting Started

### 1. Docker Setup (Local Development)
1. Navigate to the `docker/` directory.
2. Run the stack:
   ```bash
   docker-compose up -d
   ```
3. Verify services are running:
   - **MQTT**: `localhost:1883`
   - **InfluxDB**: `localhost:8086` (User: `admin`, Pass: `adminpassword`, Token: `mysecrettoken`)
   - **Grafana**: `localhost:3000` (User: `admin`, Pass: `admin`)

### 2. Firmware Configuration
1. Open `firmware/src/firmware.ino` in Arduino IDE.
2. Edit `firmware/src/secrets.h`:
   - Set `WIFI_SSID` and `WIFI_PASS`.
   - Set `MQTT_BROKER` to your computer's local IP address (e.g., `192.168.1.50`).
3. Select Board: **ESP32S3 Dev Module**.
4. Enable **USB CDC On Boot** in Tools menu.
5. Upload to ESP32-S3.

### 3. Grafana Visualization
1. Log in to Grafana at `http://localhost:3000`.
2. Add a **Data Source**:
   - Type: **InfluxDB**.
   - Query Language: **Flux**.
   - URL: `http://influxdb:8086`.
   - Org: `weatherstation`.
   - Token: `mysecrettoken`.
   - Bucket: `sensors`.
3. Create a Dashboard and add panels for Temperature and Humidity.
   - Example Flux Query for Temperature:
     ```flux
     from(bucket: "sensors")
       |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
       |> filter(fn: (r) => r["_measurement"] == "mqtt_consumer")
       |> filter(fn: (r) => r["topic"] == "sensors/esp32/temperature")
       |> yield(name: "mean")
     ```

## Moving to Raspberry Pi
1. Copy the `docker/` directory to your Raspberry Pi.
2. Run `docker-compose up -d` on the Pi.
3. Update `MQTT_BROKER` in `secrets.h` to the Pi's IP address and re-upload the firmware.

## Security Note
The current setup uses anonymous MQTT and default credentials for InfluxDB/Grafana. For production use, please:
1. Enable authentication in `mosquitto.conf`.
2. Update MQTT client credentials in `firmware.ino`.
3. Change default passwords in `docker-compose.yml`.
