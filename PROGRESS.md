# ESP32-S3 Weather Station — Current Progress

_Snapshot as of 2026-04-28._

## Architecture (current)

```
[XY-MD02 @0x01] --------RS485--> [ESP32-S3] --+-- MQTT JSON  (sensors/esp32/data, QoS 1)
[Eastron SDM120 @0x02]--RS485-->  RTU Master  |
                                             +-- Modbus TCP Slave  (port 502, HR0..HR20)
                                             |
                                             +-- mDNS  (esp32s3-weather.local)

[Raspberry Pi 4 @ raspberrypi4.local / 10.42.0.1]
  └── Docker Compose:
        ├── mosquitto   (MQTT broker)   :1883, :9001
        ├── influxdb    (v2.7)          :8086
        ├── telegraf    (JSON, QoS 1 sub)
        └── grafana                     :3000
```

## Deployment state

| Component          | Location                          | State    |
|--------------------|-----------------------------------|----------|
| MQTT broker        | `raspberrypi4.local:1883`         | ✅ running |
| InfluxDB           | `http://raspberrypi4.local:8086`  | ✅ running |
| Telegraf           | Pi (docker container)             | ✅ running (host telemetry enabled) |
| Grafana            | `http://raspberrypi4.local:3000`  | ✅ running |
| Firmware (flashed) | ESP32 on WiFi `YOUR_SSID`              | ✅ reflashed with new firmware; publishes JSON @ QoS 1 to Pi broker |
| Firmware (source)  | `firmware/src/firmware/firmware.ino` | ✅ MQTT JSON includes weather + SDM120 power fields (LWT still TBD) |
| Grafana dashboard  | `Temperature & Humidity` (uid `ad8k4s5`) | ✅ cloned from laptop → Pi via API; InfluxDB datasource uid pinned so panels resolve unchanged |
| Laptop docker stack | `weatherstation_docker/`          | ✅ stopped (`docker compose down`) after dashboard export |

## What changed this session

1. **Merged firmware** — combined RTU master + MQTT publisher (original) with Modbus TCP slave + mDNS (separate sketch) into one firmware. Non-blocking MQTT reconnect so `mb.task()` keeps servicing TCP clients.
2. **Modbus TCP slave verified** — polled via `mbpoll`; holding registers `HR0..HR3` return temp (×10), humidity (×10), status, poll counter.
3. **Swapped MQTT library** — PubSubClient → `MQTT` by Joel Gaehwiler (256dpi/arduino-mqtt). PubSubClient can only do QoS 0 publishes; this library supports QoS 1/2.
4. **Single JSON telemetry topic** — replaced `sensors/esp32/{temperature,humidity,status}` with one topic `sensors/esp32/data` carrying weather and SDM120 fields:
   ```json
   {"status": 0, "poll_count": 142, "temperature": 24.9, "humidity": 48.6, "power_status": 0, "power_voltage": 229.4, "power_current": 0.418, "power_watts": 74.6}
   ```
   Published at **QoS 1**, retained=false. On device errors, the failing device's value fields are omitted while its status code is still sent.
5. **Telegraf updated** — `data_format = "json"`, `qos = 1`, `name_override = "weather"`. Each JSON key is now its own `_field` under measurement `weather`.
6. **README rewritten** — new architecture, Flux queries updated for the `weather` measurement, library list, mbpoll section, security note now covers Modbus TCP too.
7. **Deployed to Raspberry Pi 4** — `~/weather-station/weatherstation_docker/` on the Pi, stack started, all four containers verified listening.

## Key files

- [firmware/src/firmware/firmware.ino](firmware/src/firmware/firmware.ino) — combined firmware
- [firmware/src/firmware/secrets.h](firmware/src/firmware/secrets.h) — WiFi + MQTT broker IP (still `10.42.0.65`, needs update to `10.42.0.1`)
- [weatherstation_docker/docker-compose.yml](weatherstation_docker/docker-compose.yml) — TIG + MQTT stack
- [weatherstation_docker/telegraf.conf](weatherstation_docker/telegraf.conf) — JSON/QoS 1 consumer
- [weatherstation_docker/mosquitto.conf](weatherstation_docker/mosquitto.conf) — anonymous listener on 1883
- [README.md](README.md) — full setup walkthrough

## Pending / open items

- [x] **Remote access to Grafana** — Cloudflare Tunnel + Cloudflare Access (implemented). Added `cloudflared` service and updated Grafana environment variables.
- [x] **Pi host telemetry** — Telegraf now publishes Pi CPU/RAM/temp metrics direct to InfluxDB. Added `[[inputs.cpu]]`, `mem`, `system`, `disk`, and `file` (CPU temp) with host volume mounts.
- [x] **SDM120 power meter support** — firmware polls SDM120 via FC04, publishes MQTT JSON power fields, exposes IEEE754 float pairs over Modbus TCP, and adds Flux/dashboard docs.

## Project re-review TODO

Added 2026-04-27 and repo-reviewed on 2026-04-28. Hardware-required checks are captured in [docs/project_review.md](docs/project_review.md).

- [x] **Firmware integration review** — compile/static review complete; real RS485 bus verification remains hardware-required.
- [x] **MQTT payload review** — expected payload and `jq` verification command documented; live Mosquitto tail remains hardware-required.
- [x] **InfluxDB/Telegraf review** — expanded JSON fields and env-based Influx config reviewed; live field/type confirmation remains runtime-required.
- [x] **Grafana review** — added `weatherstation_docker/power_meter_template.json` with SDM120 status and power panels.
- [x] **Modbus TCP review** — documented `HR0..HR20`, raw poll command, and float-pair decoding path.
- [x] **Docs review** — refreshed README, QUICK_START, Flux examples, kiosk setup, `dataflow.png`, and added the project review runbook.
- [x] **Deployment review** — reconciled duplicate `docker/` and `weatherstation_docker/` configs and documented `weatherstation_docker/` as canonical.
- [x] **Security review** — moved compose secrets to `.env` templates and ignored local `.env`; MQTT authentication and TLS remain production follow-ups.
- [x] **Operations review** — switched Mosquitto to stdout logging with Docker log rotation and documented backup/recovery checks.
- [x] **Release readiness review** — documented `arduino-cli` compile command, dependencies, and pinned Docker image defaults.

## Production readiness — further development plan

Audit on 2026-04-17 identified the following gaps before this stack can be considered production-ready. Ordered by priority.

### 🔴 Critical (blockers for remote exposure)

- [x] **Move secrets out of git** — compose and Telegraf now read from `.env`/environment variables and `.env` is gitignored. Rotate all previously used defaults before production.
- [ ] **Enable MQTT authentication** — `mosquitto.conf` has `allow_anonymous true`. Any device on the LAN can publish fake sensor data or snoop telemetry. Generate a password file, set `allow_anonymous false`, update firmware (`mqttClient.connect(CLIENT_ID, user, pass)`).
- [x] **Cloudflare tunnel token** — compose now reads `CLOUDFLARE_TUNNEL_TOKEN` from `.env` and gates cloudflared behind the optional `tunnel` profile.

### 🟡 High

- [x] **Pin image versions** — compose defaults now pin Mosquitto, InfluxDB, Telegraf, Grafana, and cloudflared images while allowing `.env` overrides.
- [ ] **MQTT Last Will & Testament** — no way to detect ESP32 crashes/disconnects:
   ```cpp
   mqttClient.setWill("sensors/esp32/online", "0", true, 1);   // in setup
   mqttClient.publish("sensors/esp32/online", "1", true, 1);   // after successful connect
   ```
- [ ] **InfluxDB backup strategy** — a disk failure on the Pi loses all history. Add weekly `influx backup` cron → external storage.
- [ ] **Firmware OTA** — currently requires USB to update. Consider ArduinoOTA or ESPAsyncHTTPUpdateServer.
- [ ] **Firmware data buffering** — if MQTT reconnect takes time, sensor readings are dropped. Buffer last N readings in a ring buffer and replay on reconnect.

### 🟢 Medium

- [ ] **Container health checks** — no `healthcheck:` blocks. Docker can't auto-restart stuck containers.
- [ ] **Resource limits** — no `mem_limit` / `cpus:` constraints; a runaway container can OOM the Pi.
- [x] **Mosquitto log rotation** — Mosquitto logs to stdout and Docker log options cap container log files.
- [ ] **TLS everywhere** — MQTT over port 8883 with a cert, InfluxDB/Grafana behind TLS even on LAN.
- [x] **Remove placeholder** — Grafana root URL/domain now come from `.env` instead of a committed placeholder.

### 🔵 Nice to have

- [ ] **Multiple ESP32 nodes** — distinct client IDs and topic prefixes.
- [ ] **Grafana alerting** — temperature thresholds → push notification / email.
- [ ] **Dashboard provisioning** — use Grafana's file-based config instead of manual API import.
- [ ] **Modbus TCP hardening** — port 502 has no auth. Keep off untrusted networks (already documented, just reiterate on deployment).

### ✅ What's already production-grade

- `secrets.h` properly gitignored with a template (`secrets.h.example`).
- Firmware uses non-blocking MQTT reconnect so `mb.task()` keeps servicing TCP clients.
- RTU CRC-16 validation on every poll.
- Telegraf JSON parsing is clean and flattens each key into its own `_field`.
- Git history is clean — no WiFi/MQTT credentials ever committed.

## Useful commands

### Verify Modbus TCP slave (from any machine on WiFi)
```bash
mbpoll -a 1 -t 4 -r 1 -c 21 esp32s3-weather.local
```
Expect weather registers plus SDM120 status and IEEE754 float pairs. See [docs/project_review.md](docs/project_review.md) for the full map.

### Tail MQTT traffic from the Pi broker
```bash
mosquitto_sub -h raspberrypi4.local -t '#' -v
```

### Manage the Pi stack
```bash
ssh pi@raspberrypi4.local 'cd ~/weather-station/weatherstation_docker && docker compose ps'
ssh pi@raspberrypi4.local 'cd ~/weather-station/weatherstation_docker && docker compose logs -f mosquitto'
ssh pi@raspberrypi4.local 'cd ~/weather-station/weatherstation_docker && docker compose restart telegraf'
ssh pi@raspberrypi4.local 'cd ~/weather-station/weatherstation_docker && docker compose down'
```

### Flux smoke test (once dashboards wired up)
```flux
from(bucket: "sensors")
  |> range(start: -5m)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> last()
```
