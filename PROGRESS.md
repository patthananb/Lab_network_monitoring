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
| Firmware (flashed) | ESP32 on WiFi `ESL2`              | ✅ reflashed with new firmware; publishes JSON @ QoS 1 to Pi broker |
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
- [ ] **Liveness signal** — discussed but not implemented. Add an MQTT Last Will & Testament so `sensors/esp32/online` flips to `"0"` if the ESP32 disconnects ungracefully:
   ```cpp
   mqttClient.setWill("sensors/esp32/online", "0", true, 1);   // in setup
   mqttClient.publish("sensors/esp32/online", "1", true, 1);   // after successful connect
   ```
- [x] **Pi host telemetry** — Telegraf now publishes Pi CPU/RAM/temp metrics direct to InfluxDB. Added `[[inputs.cpu]]`, `mem`, `system`, `disk`, and `file` (CPU temp) with host volume mounts.
- [ ] **Security hardening** — currently anonymous MQTT, anonymous Modbus TCP, default InfluxDB/Grafana passwords. Fine for trusted LAN, critical to address before any remote exposure.

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
