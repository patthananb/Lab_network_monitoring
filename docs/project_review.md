# Project Review Runbook

_Last updated: 2026-04-28._

This runbook turns the project re-review checklist into repeatable checks. Items marked "repo-verified" were checked from source/configuration. Items marked "hardware-required" need the ESP32, XY-MD02, SDM120, and Raspberry Pi stack online.

## Firmware Integration

Status: repo-verified; hardware-required for final sign-off.

- Compile the firmware:
  ```bash
  arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/src/firmware
  ```
- Confirm RS485 topology before flashing:
  - XY-MD02 address: `0x01`
  - SDM120 address: `0x02`
  - Both devices use FC04 input-register reads at 9600 baud.
  - ESP32-S3 RS485 pins are RX `GPIO18`, TX `GPIO17`.
- Confirm serial output after flashing:
  ```text
  XY-MD02: <temperature>C  <humidity>%RH  [OK #<count>]
  SDM120: <voltage>V  <current>A  <watts>W  <va>VA  <var>VAr  pf=<factor>  <frequency>Hz  <energy>kWh [OK]
  ```
- If SDM120 reads fail, verify meter address, A/B polarity, termination, and that no other device on the bus uses address `0x02`.

## MQTT Payload

Status: hardware-required.

Tail one message from the Pi broker:

```bash
docker compose exec mosquitto \
  mosquitto_sub -C 1 -t "sensors/esp32/data" |
  jq -e '
    has("status") and
    has("poll_count") and
    has("temperature") and
    has("humidity") and
    has("power_status") and
    has("power_voltage") and
    has("power_current") and
    has("power_watts") and
    has("power_apparent_va") and
    has("power_reactive_var") and
    has("power_factor") and
    has("power_frequency") and
    has("power_energy_kwh")
  '
```

Expected good payload shape:

```json
{"status":0,"poll_count":1234,"temperature":24.5,"humidity":48.3,"power_status":0,"power_voltage":229.4,"power_current":0.418,"power_watts":74.6,"power_apparent_va":77.5,"power_reactive_var":12.0,"power_factor":0.963,"power_frequency":50.0,"power_energy_kwh":12.348}
```

During a weather-sensor error, `status` should be non-zero and weather value fields should be omitted. During an SDM120 error, `power_status` should be non-zero and power value fields should be omitted.

## InfluxDB And Telegraf

Status: repo-verified; hardware-required for final sign-off.

- Telegraf reads `sensors/esp32/data` with `data_format = "json"` and `name_override = "weather"`.
- InfluxDB connection settings are supplied by Docker Compose environment variables.
- Confirm fields after live MQTT data is flowing:
  ```bash
  docker compose exec influxdb influx query '
  import "influxdata/influxdb/schema"
  schema.fieldKeys(bucket: "sensors", predicate: (r) => r._measurement == "weather")
  '
  ```
- Confirm the power fields are numeric by querying recent values:
  ```flux
  from(bucket: "sensors")
    |> range(start: -15m)
    |> filter(fn: (r) => r["_measurement"] == "weather")
    |> filter(fn: (r) => r["_field"] == "power_watts" or r["_field"] == "power_voltage")
    |> last()
  ```

## Grafana

Status: repo-verified; import-required for final sign-off.

- Import `weatherstation_docker/power_meter_template.json`.
- Confirm the dashboard title is `SDM120 Power Meter`.
- Confirm panels render:
  - SDM120 Status
  - Voltage
  - Current
  - Active Power
  - Power Over Time
  - Power Factor and Frequency
  - Total Active Energy

## Modbus TCP

Status: hardware-required.

Poll the ESP32 Modbus TCP slave:

```bash
mbpoll -a 1 -t 4 -r 1 -c 21 esp32s3-weather.local
```

Expected register map:

| Register | Meaning |
|---|---|
| HR0 | Temperature, raw x 0.1 C |
| HR1 | Humidity, raw x 0.1 %RH |
| HR2 | Weather status |
| HR3 | Poll count |
| HR4 | SDM120 status |
| HR5-HR20 | SDM120 IEEE754 float pairs: voltage, current, W, VA, VAr, power factor, Hz, kWh |

If your `mbpoll` build supports float register types, this should decode the SDM120 float pairs directly:

```bash
mbpoll -a 1 -t 4:float -r 6 -c 8 esp32s3-weather.local
```

## Deployment

Status: repo-verified.

- `weatherstation_docker/` is the canonical Pi deployment directory used by the README and Quick Start.
- `docker/` is kept as a mirrored legacy directory; its compose, Telegraf, and Mosquitto config should stay identical to `weatherstation_docker/`.
- Validate both compose files:
  ```bash
  docker compose --env-file weatherstation_docker/.env.example -f weatherstation_docker/docker-compose.yml config >/dev/null
  docker compose --env-file docker/.env.example -f docker/docker-compose.yml config >/dev/null
  diff -u weatherstation_docker/docker-compose.yml docker/docker-compose.yml
  diff -u weatherstation_docker/telegraf.conf docker/telegraf.conf
  diff -u weatherstation_docker/mosquitto.conf docker/mosquitto.conf
  ```

## Security

Status: partially remediated; production changes still required.

- `.env.example` now documents non-committed secrets for both compose directories.
- `*.env` files are ignored by git.
- Docker images are pinned by default and can be overridden from `.env`.
- Still required before untrusted exposure:
  - Rotate default InfluxDB and Grafana credentials.
  - Disable anonymous MQTT and add firmware MQTT username/password support.
  - Put Cloudflare tunnel token only in `.env`.
  - Keep Modbus TCP limited to a trusted LAN/VLAN.

## Operations

Status: repo-verified; runtime-required for final sign-off.

- Mosquitto logs to stdout and Docker rotates container logs.
- Confirm stack status:
  ```bash
  docker compose ps
  docker compose logs --tail=100 mosquitto telegraf influxdb grafana
  ```
- Back up InfluxDB regularly:
  ```bash
  docker compose exec influxdb influx backup /tmp/influx-backup
  docker cp influxdb:/tmp/influx-backup ./influx-backup
  ```
- Test recovery after power loss by rebooting the Pi and confirming MQTT, InfluxDB, Grafana, and the ESP32 reconnect path all recover.

## Release Readiness

Status: repo-verified.

- Known-good local compile command:
  ```bash
  arduino-cli compile --fqbn esp32:esp32:esp32s3 firmware/src/firmware
  ```
- Arduino dependencies:
  - ESP32 core: `esp32:esp32`
  - Arduino MQTT library by Joel Gaehwiler
  - `modbus-esp8266` by emelianov
- Docker image defaults are declared in `.env.example`.
