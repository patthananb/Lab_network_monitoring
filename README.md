# ESP32-S3 Weather Station (TIG Stack + MQTT)

A weather monitoring system using an ESP32-S3, Modbus RTU sensor, and a TIG (Telegraf, InfluxDB, Grafana) stack for data visualization.

## 🚀 Quick Start

### 1. Docker Setup
```bash
cd weatherstation_docker/
docker-compose up -d
```
- **MQTT**: `localhost:1883`
- **InfluxDB**: `localhost:8086` (Token: `mysecrettoken`)
- **Grafana**: `localhost:3000` (User/Pass: `admin/admin`)

### 2. Firmware Setup
1. Open `firmware/src/firmware/firmware.ino` in Arduino IDE.
2. Configure `secrets.h` with WiFi and MQTT Broker IP.
3. Install libraries: `MQTT` (Joel Gaehwiler), `modbus-esp8266`.
4. Board: **ESP32S3 Dev Module** with **USB CDC On Boot** enabled.
5. Upload.

## 🏗️ Architecture
- **ESP32-S3**: Polls sensor via RS485 (Modbus RTU), publishes JSON to MQTT, and serves as Modbus TCP Slave.
- **TIG Stack**: Dockerized Telegraf (collection), InfluxDB (storage), and Grafana (visualization).
- **Network**: Uses mDNS (`esp32s3-weather.local`) for easy discovery.

## 📊 Data & Visualization

### Grafana Setup
1. Connect InfluxDB as a data source (Flux, `http://influxdb:8086`, Org: `weatherstation`, Token: `mysecrettoken`).
2. Build dashboards using the telemetry fields: `temperature`, `humidity`, `status`, `poll_count`.

> 💡 **Detailed Flux queries** for all metrics can be found in [docs/flux_queries.md](docs/flux_queries.md).

<details>
<summary><b>🔍 Optional: Modbus TCP Verification</b></summary>

The ESP32 serves sensor data on port 502. Use `mbpoll` to verify:
```bash
mbpoll -a 1 -t 4 -r 1 -c 4 esp32s3-weather.local
```
| HREG | Field | Encoding |
|------|-------|----------|
| 0 | Temperature | raw × 0.1 °C, signed |
| 1 | Humidity | raw × 0.1 %RH, unsigned |
| 2 | Status | 0=OK, 1=timeout, 2=CRC err, 3=exception |
| 3 | Poll count | uint16, increments per RTU poll |
</details>

<details>
<summary><b>🥧 Pi Host Telemetry</b></summary>

Telegraf also collects Raspberry Pi system metrics (CPU, RAM, Disk, Temperature) and stores them in the `sensors` bucket. See [docs/flux_queries.md](docs/flux_queries.md) for examples.
</details>

## 🚢 Deployment & Security
- **Moving to Production**: Copy `weatherstation_docker/` to your Pi and update `MQTT_BROKER` in the firmware.
- **Security**: This is a LAN-only setup. See [PROGRESS.md](PROGRESS.md) for the production readiness checklist (Authentication, Secrets management, TLS).

## 🛠️ Project Structure
```text
.
├── weatherstation_docker/   # TIG + MQTT Stack configurations
└── firmware/
    └── src/
        └── firmware/        # ESP32 Source Code
```

## 📈 Roadmap
Ongoing development tasks and production-readiness gaps are tracked in **[PROGRESS.md](PROGRESS.md)**.
