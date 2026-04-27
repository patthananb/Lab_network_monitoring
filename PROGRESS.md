# ESP32-S3 Weather Station — Current Progress

_Snapshot as of 2026-04-17._

## Architecture (current)

```
[XY-MD02 @0x01] --RS485--> [ESP32-S3] --+-- MQTT JSON  (sensors/esp32/data, QoS 1)
                 FC04       RTU Master  |
                                        +-- Modbus TCP Slave  (port 502)
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
| Firmware (source)  | `firmware/src/firmware/firmware.ino` | ✅ MQTT lib swapped, JSON, QoS 1 (LWT still TBD) |
| Grafana dashboard  | `Temperature & Humidity` (uid `ad8k4s5`) | ✅ cloned from laptop → Pi via API; InfluxDB datasource uid pinned so panels resolve unchanged |
| Laptop docker stack | `weatherstation_docker/`          | ✅ stopped (`docker compose down`) after dashboard export |

## What changed this session

1. **Merged firmware** — combined RTU master + MQTT publisher (original) with Modbus TCP slave + mDNS (separate sketch) into one firmware. Non-blocking MQTT reconnect so `mb.task()` keeps servicing TCP clients.
2. **Modbus TCP slave verified** — polled via `mbpoll`; holding registers `HR0..HR3` return temp (×10), humidity (×10), status, poll counter.
3. **Swapped MQTT library** — PubSubClient → `MQTT` by Joel Gaehwiler (256dpi/arduino-mqtt). PubSubClient can only do QoS 0 publishes; this library supports QoS 1/2.
4. **Single JSON telemetry topic** — replaced `sensors/esp32/{temperature,humidity,status}` with one topic `sensors/esp32/data` carrying:
   ```json
   {"temperature": 24.9, "humidity": 48.6, "status": 0, "poll_count": 142}
   ```
   Published at **QoS 1**, retained=false. On error, only `{"status": N, "poll_count": N}` is sent.
5. **Telegraf updated** — `data_format = "json"`, `qos = 1`, `name_override = "weather"`. Each JSON key is now its own `_field` under measurement `weather`.
6. **README rewritten** — new architecture, Flux queries updated for the `weather` measurement, library list, mbpoll section, security note now covers Modbus TCP too.
7. **Deployed to Raspberry Pi 4** — `~/weather-station/docker/` on the Pi, stack started, all four containers verified listening.

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

## Project re-review TODO

Added 2026-04-27 as a checklist for the next full project pass.

- [ ] **Firmware integration review** — verify XY-MD02 and SDM120 can share the RS485 bus reliably, confirm SDM120 address `0x02`, FC04 register reads, float word order, timeout behavior, and serial output on real hardware.
- [ ] **MQTT payload review** — tail `sensors/esp32/data` from Mosquitto and confirm weather fields, SDM120 fields, `status`, `power_status`, and omitted-value behavior during device errors.
- [ ] **InfluxDB/Telegraf review** — confirm the expanded JSON fields are parsed as numeric fields under measurement `weather` with no type conflicts from older data.
- [ ] **Grafana review** — add or update dashboard panels for voltage, current, watts, VA, VAr, power factor, frequency, energy, and SDM120 status.
- [ ] **Modbus TCP review** — poll `HR0..HR20`, decode SDM120 IEEE754 float register pairs, and document the exact verification command/output.
- [ ] **Docs review** — refresh README, QUICK_START, Flux examples, kiosk docs, and `dataflow.png` so architecture diagrams and examples match the current firmware.
- [ ] **Deployment review** — reconcile duplicate `docker/` and `weatherstation_docker/` configs, confirm the Pi deployment path, and make sure compose files match the deployed stack.
- [ ] **Security review** — revisit MQTT authentication, hardcoded Influx/Grafana secrets, Cloudflare tunnel token handling, TLS, and Modbus TCP LAN exposure.
- [ ] **Operations review** — check backups, container health checks, resource limits, Mosquitto log rotation, and recovery steps after power loss.
- [ ] **Release readiness review** — decide whether to add CI/build notes for `arduino-cli`, pin library/core versions, and record a known-good firmware upload procedure.

## Production readiness — further development plan

Audit on 2026-04-17 identified the following gaps before this stack can be considered production-ready. Ordered by priority.

### 🔴 Critical (blockers for remote exposure)

- [ ] **Move secrets out of git** — `docker-compose.yml` hardcodes `mysecrettoken` (InfluxDB token), `adminpassword` (InfluxDB), `admin/admin` (Grafana). `telegraf.conf` also hardcodes the InfluxDB token. Move to a `.env` file referenced via `${VAR}` syntax; add `.env` to `.gitignore`; rotate all tokens/passwords.
- [ ] **Enable MQTT authentication** — `mosquitto.conf` has `allow_anonymous true`. Any device on the LAN can publish fake sensor data or snoop telemetry. Generate a password file, set `allow_anonymous false`, update firmware (`mqttClient.connect(CLIENT_ID, user, pass)`).
- [ ] **Cloudflare tunnel token** — `docker-compose.yml:72` has `TUNNEL_TOKEN=PASTE_YOUR_TOKEN_HERE`. Once real token is pasted it would be committed. Move to `.env`.

### 🟡 High

- [ ] **Pin image versions** — `mosquitto:latest`, `grafana/grafana:latest`, `cloudflared:latest` make upgrades non-deterministic. Pin to explicit tags.
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
- [ ] **Mosquitto log rotation** — `mosquitto.log` grows unbounded in the volume.
- [ ] **TLS everywhere** — MQTT over port 8883 with a cert, InfluxDB/Grafana behind TLS even on LAN.
- [ ] **Remove placeholder** — `GF_SERVER_ROOT_URL=https://grafana.yourdomain.com` in compose is a dead placeholder until the real hostname is decided.

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
mbpoll -a 1 -t 4 -r 1 -c 4 esp32s3-weather.local
```
Expect 4 registers: `[1]=tempx10`, `[2]=humix10`, `[3]=status`, `[4]=pollcount`.

### Tail MQTT traffic from the Pi broker
```bash
mosquitto_sub -h raspberrypi4.local -t '#' -v
```

### Manage the Pi stack
```bash
ssh pi@raspberrypi4.local 'cd ~/weather-station/docker && docker compose ps'
ssh pi@raspberrypi4.local 'cd ~/weather-station/docker && docker compose logs -f mosquitto'
ssh pi@raspberrypi4.local 'cd ~/weather-station/docker && docker compose restart telegraf'
ssh pi@raspberrypi4.local 'cd ~/weather-station/docker && docker compose down'
```

### Flux smoke test (once dashboards wired up)
```flux
from(bucket: "sensors")
  |> range(start: -5m)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> last()
```
