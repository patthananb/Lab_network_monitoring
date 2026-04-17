# ESP32-S3 Weather Station (TIG Stack + MQTT)

This project implements a weather monitoring system using an ESP32-S3, an XY-MD02 Modbus RTU sensor, and a TIG (Telegraf, InfluxDB, Grafana) stack for data visualization, with MQTT as the communication bridge.

## Architecture
- **Sensor**: XY-MD02 (Temperature & Humidity) via RS485 (Modbus RTU).
- **Edge Device**: ESP32-S3 (Waveshare Relay Board). Polls the sensor every 2 s and:
  - Publishes a JSON telemetry message to MQTT at QoS 1 (`sensors/esp32/data`).
  - Exposes the same reading as a Modbus TCP Slave on port 502.
  - Advertises itself over mDNS as `esp32s3-weather.local`.
- **Broker**: Eclipse Mosquitto (MQTT).
- **Collector**: Telegraf — subscribes to the MQTT topic and parses JSON into InfluxDB.
- **Storage**: InfluxDB v2.
- **Visualization**: Grafana.

## Hardware Requirements
- **Waveshare ESP32-S3-Relay-6CH** (or any ESP32-S3 with RS485 transceiver).
- **XY-MD02** Temperature & Humidity Sensor.
- **Raspberry Pi** (Production) or **PC with Docker** (Development).

## Software Requirements
- **Arduino IDE** with ESP32 Board Support.
- **Libraries**:
  - `MQTT` by Joel Gaehwiler (256dpi/arduino-mqtt) — QoS 1 publisher.
  - `modbus-esp8266` by emelianov — Modbus TCP Slave.
- **Docker & Docker Compose**.

## Project Structure
```text
.
├── weatherstation_docker/
│   ├── docker-compose.yml   # TIG + MQTT Stack
│   ├── mosquitto.conf       # MQTT Broker Config
│   └── telegraf.conf        # Data Collection Config (JSON parser, QoS 1, Pi metrics)
└── firmware/
    └── src/
        └── firmware/
            ├── firmware.ino # ESP32 Source Code (RTU master + TCP slave + MQTT)
            └── secrets.h    # WiFi & MQTT Credentials
```

## Getting Started

### 1. Docker Setup (Local Development)
1. Navigate to the `weatherstation_docker/` directory.
2. Run the stack:
   ```bash
   docker-compose up -d
   ```
3. Verify services are running:
   - **MQTT**: `localhost:1883`
   - **InfluxDB**: `localhost:8086` (User: `admin`, Pass: `adminpassword`, Token: `mysecrettoken`)
   - **Grafana**: `localhost:3000` (User: `admin`, Pass: `admin`)

### 2. Firmware Configuration
1. Open `firmware/src/firmware/firmware.ino` in Arduino IDE.
2. Edit `firmware/src/firmware/secrets.h`:
   - Set `WIFI_SSID` and `WIFI_PASS`.
   - Set `MQTT_BROKER` to your computer's local IP address (e.g., `192.168.1.50`).
3. In **Library Manager**, install:
   - **MQTT** by Joel Gaehwiler (`256dpi/arduino-mqtt`).
   - **modbus-esp8266** by emelianov.
4. Select Board: **ESP32S3 Dev Module**.
5. Enable **USB CDC On Boot** in Tools menu.
6. Upload to ESP32-S3.

On boot the serial monitor (115200) prints the assigned IP, mDNS hostname, and MQTT connection status.

### 3. Modbus TCP (Optional Verification)
The firmware also serves the most-recent sensor reading over Modbus TCP on port 502, advertised via mDNS as `esp32s3-weather.local`.

Install `mbpoll`:
```bash
brew install mbpoll          # macOS
sudo apt install mbpoll      # Raspberry Pi / Debian
```

Poll all four holding registers at 1 Hz (Ctrl-C to stop):
```bash
mbpoll -a 1 -t 4 -r 1 -c 4 esp32s3-weather.local
```

Holding register map (FC03, 0-based):

| HREG | Field       | Encoding                                   |
|------|-------------|--------------------------------------------|
| 0    | Temperature | raw × 0.1 °C, signed (e.g. `249` = 24.9)   |
| 1    | Humidity    | raw × 0.1 %RH, unsigned (e.g. `486` = 48.6)|
| 2    | Status      | 0=OK, 1=timeout, 2=CRC err, 3=exception    |
| 3    | Poll count  | uint16, increments per RTU poll (rolls at 65535) |

### 4. Grafana Visualization
1. Log in to Grafana at `http://localhost:3000`.
2. Add a **Data Source**:
   - Type: **InfluxDB**.
   - Query Language: **Flux**.
   - URL: `http://influxdb:8086`.
   - Org: `weatherstation`.
   - Token: `mysecrettoken`.
   - Bucket: `sensors`.
3. Create a Dashboard and add panels using the Flux queries below.

#### Flux Queries

The ESP32 publishes a single JSON message to `sensors/esp32/data` at QoS 1. Telegraf flattens each JSON key into its own field under measurement `weather`:

| `_field`      | Source             | Notes                                      |
|---------------|--------------------|--------------------------------------------|
| `temperature` | `temperature` key  | °C, float                                  |
| `humidity`    | `humidity` key     | %RH, float                                 |
| `status`      | `status` key       | 0=OK, 1=timeout, 2=CRC err, 3=exception    |
| `poll_count`  | `poll_count` key   | RTU cycles since boot                      |

**Temperature (°C):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "temperature")
  |> yield(name: "temperature")
```

**Humidity (%RH):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "humidity")
  |> yield(name: "humidity")
```

**ESP Status:**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "status")
  |> yield(name: "status")
```

**Poll Count:**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "poll_count")
  |> yield(name: "poll_count")
```

**All fields in one query** (Grafana splits series by `_field`):
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
```

#### Query Variants

- **Smoothed for time-series panels** — insert before `yield`:
  ```flux
    |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  ```
- **Latest value for Stat / Gauge panels** — append instead of or after `yield`:
  ```flux
    |> last()
  ```

### 5. Pi Host Telemetry
Telegraf is configured to collect host metrics from the Raspberry Pi. This data is stored in the same `sensors` bucket.

**Measurements available:**
- `cpu`: `usage_user`, `usage_system`, `usage_idle`, etc.
- `mem`: `used_percent`, `available`, etc.
- `system`: `load1`, `uptime`, etc.
- `disk`: `free`, `used_percent`, etc.
- `pi_cpu_temp`: raw value in milli-Celsius (divide by 1000 for °C).

**Example Flux Query for Pi CPU Temp (°C):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "pi_cpu_temp")
  |> map(fn: (r) => ({ r with _value: float(v: r._value) / 1000.0 }))
  |> yield(name: "pi_cpu_temp")
```

## Moving to Raspberry Pi
1. Copy the `weatherstation_docker/` directory to your Raspberry Pi.
2. Run `docker compose up -d` on the Pi.
3. Update `MQTT_BROKER` in `firmware/src/firmware/secrets.h` to the Pi's IP address and re-upload the firmware.

## Security Note
The current setup uses anonymous MQTT, an unauthenticated Modbus TCP slave, and default credentials for InfluxDB/Grafana. For production use, please:
1. Enable authentication in `mosquitto.conf` and update client credentials in `firmware.ino`.
2. Keep the ESP32's Modbus TCP port (502) off any untrusted network — it has no authentication.
3. Change default passwords in `docker-compose.yml`.

## Roadmap / Further Development

Items below are tracked for moving this stack from "trusted LAN" to "production-ready". Ordered by priority. See `PROGRESS.md` for the living checklist.

### Critical (before any remote exposure)
- **Secrets out of git** — move `mysecrettoken`, InfluxDB/Grafana passwords, and the Cloudflare tunnel token out of `docker-compose.yml` / `telegraf.conf` into a `.env` file (gitignored), referenced via `${VAR}` syntax. Rotate all tokens/passwords after.
- **MQTT authentication** — disable `allow_anonymous` in `mosquitto.conf`, generate a password file (`mosquitto_passwd`), and update `firmware.ino` to authenticate on connect.
- **Modbus TCP scope** — keep port 502 isolated on a trusted VLAN; it has no authentication by design.

### High priority
- **Pin Docker image versions** — replace `:latest` with explicit tags for `mosquitto`, `grafana`, and `cloudflared` for reproducible deploys.
- **MQTT Last Will & Testament** — publish `sensors/esp32/online=1` on connect, LWT `sensors/esp32/online=0` so Grafana can flag an offline ESP32.
- **InfluxDB backups** — weekly `influx backup` cron on the Pi, pushed to off-device storage.
- **Firmware OTA** — eliminate the USB round-trip for firmware updates (ArduinoOTA or HTTP update server).
- **Firmware data buffering** — ring buffer of recent readings so MQTT reconnect gaps don't lose data.

### Medium priority
- **Container health checks** — add `healthcheck:` blocks so Docker restarts stuck containers.
- **Resource limits** — `mem_limit` / `cpus:` on each service to protect the Pi from a runaway container.
- **Mosquitto log rotation** — currently unbounded.
- **TLS everywhere** — MQTT over port 8883 with a cert, InfluxDB/Grafana behind TLS even on LAN.

### Nice to have
- Multiple ESP32 nodes with distinct client IDs and topic prefixes.
- Grafana alerting (temperature thresholds → push notification / email).
- Dashboard provisioning via Grafana's file-based config instead of API import.
