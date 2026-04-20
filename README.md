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

## Querying Guide

### Grafana `v` Variables

Every Flux query in Grafana has access to a built-in `v` object injected automatically by the InfluxDB datasource:

| Variable | Description | Example value |
|---|---|---|
| `v.timeRangeStart` | Start of the dashboard time picker | `now-24h` |
| `v.timeRangeStop` | End of the dashboard time picker | `now` |
| `v.windowPeriod` | Auto-calculated interval that fits the panel width | `5m` |

You never define `v` — Grafana updates it whenever the user changes the time range and reruns all queries automatically.

### Fixing "Too Many Datapoints"

Grafana will warn when a query returns more points than the panel can draw. Fix it by adding `aggregateWindow()` before `yield()`:

```flux
|> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
```

`v.windowPeriod` auto-sizes the bucket so the number of returned points always matches the panel's pixel width.

### `aggregateWindow()` Reference

```flux
|> aggregateWindow(every: <duration>, fn: <function>, createEmpty: <bool>)
```

| Parameter | Purpose |
|---|---|
| `every` | Size of each time bucket (`1m`, `5m`, `v.windowPeriod`) |
| `fn` | Aggregation applied per bucket |
| `createEmpty` | `false` = skip empty buckets; `true` = insert nulls for gaps |

**Choosing `fn`:**

| `fn` | When to use |
|---|---|
| `mean` | Continuous sensors — temperature, humidity, CPU % |
| `last` | Status values — ping result, online/offline |
| `max` / `min` | Peaks and troughs — anomaly detection, alerts |
| `sum` | Totals — event counts, rainfall |

### Query Patterns by Panel Type

**Time-series panel** — use `aggregateWindow` to reduce points:
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "temperature")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "temperature")
```

**Stat / Gauge panel** — use `last()` to show the most recent value only:
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["_field"] == "humidity")
  |> last()
  |> yield(name: "humidity")
```

**State timeline / status panel** — use `aggregateWindow` with `fn: last` to preserve discrete state changes:
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "ping")
  |> filter(fn: (r) => r["_field"] == "result_code")
  |> aggregateWindow(every: 5m, fn: last, createEmpty: false)
```

### Current Dashboard Queries

#### Weather Sensor (`weather` measurement)

| Field | Query `fn` | Notes |
|---|---|---|
| `temperature` | `mean` | °C float from XY-MD02 |
| `humidity` | `mean` | %RH float from XY-MD02 |
| `status` | `last` | 0=OK, 1=timeout, 2=CRC err, 3=exception |
| `poll_count` | `last` | RTU cycles since boot |

**Template (replace `<field>` and `<fn>`):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "weather")
  |> filter(fn: (r) => r["host"] == "7847af5db558")
  |> filter(fn: (r) => r["_field"] == "<field>")
  |> aggregateWindow(every: v.windowPeriod, fn: <fn>, createEmpty: false)
  |> yield(name: "<field>")
```

#### Pi Telemetry (`cpu`, `mem`, `pi_cpu_temp` measurements)

**CPU Load % (total):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "cpu")
  |> filter(fn: (r) => r["cpu"] == "cpu-total")
  |> filter(fn: (r) => r["_field"] == "usage_idle")
  |> map(fn: (r) => ({r with _value: 100.0 - r._value}))
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "cpu_usage")
```

**Memory Used %:**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "mem")
  |> filter(fn: (r) => r["_field"] == "used_percent")
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "mem_usage")
```

**CPU Temperature (°C):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "pi_cpu_temp")
  |> map(fn: (r) => ({r with _value: float(v: r._value) / 1000.0}))
  |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false)
  |> yield(name: "pi_cpu_temp")
```

#### Networking (`ping` measurement)

**Current internet status (Stat panel):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "ping")
  |> filter(fn: (r) => r["_field"] == "result_code")
  |> last()
```

**Connectivity timeline (State timeline panel):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "ping")
  |> filter(fn: (r) => r["_field"] == "result_code")
  |> aggregateWindow(every: 5m, fn: last, createEmpty: false)
```

**Outage log — downtime events only (Table panel):**
```flux
from(bucket: "sensors")
  |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
  |> filter(fn: (r) => r["_measurement"] == "ping")
  |> filter(fn: (r) => r["_field"] == "result_code")
  |> filter(fn: (r) => r._value > 0)
  |> map(fn: (r) => ({_time: r._time, status: "Offline", result_code: r._value}))
  |> sort(columns: ["_time"], desc: true)
```

---

## Kiosk Display Setup (Raspberry Pi OS Lite + 7" Touchscreen)

This section covers turning a Raspberry Pi 4 running **Raspberry Pi OS Lite** (headless, no desktop) into a dedicated full-screen Grafana kiosk using a 7" touchscreen display. The result is a Pi that boots straight into the Grafana playlist with no login prompt, no mouse cursor, and no visible browser UI.

### Hardware Requirements

- Raspberry Pi 4 (2 GB or more recommended)
- Raspberry Pi Official 7" Touchscreen Display (800×480)
- MicroSD card with **Raspberry Pi OS Lite (64-bit)** flashed
- Power supply (5V 3A USB-C)

### Overview of What Gets Installed

| Package | Purpose |
|---|---|
| `xserver-xorg` | Minimal X11 display server |
| `x11-xserver-utils` | `xset` utility for disabling screen blanking |
| `xinit` | `startx` command to launch X from the console |
| `openbox` | Lightweight window manager (no taskbar, no desktop) |
| `chromium` | Browser for rendering Grafana in kiosk mode |
| `unclutter` | Hides the mouse cursor when idle |

---

### Step 1 — Install Display Packages

SSH into the Pi (or connect a keyboard) and run:

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install --no-install-recommends \
  xserver-xorg x11-xserver-utils xinit \
  openbox chromium unclutter -y
```

> **Note:** On Raspberry Pi OS Trixie and newer the package is `chromium`, not `chromium-browser`.

---

### Step 2 — Configure Openbox Autostart

Openbox reads `~/.config/openbox/autostart` when a session starts. This file disables screen sleep and launches Chromium pointed at the Grafana playlist.

```bash
mkdir -p ~/.config/openbox
nano ~/.config/openbox/autostart
```

Paste the following:

```bash
# Disable screen blanking and power saving
xset s off
xset s noblank
xset -dpms

# Hide the mouse cursor after 0.1 seconds of inactivity
unclutter -idle 0.1 -root &

# Launch Chromium in kiosk mode pointed at the Grafana playlist
chromium --noerrdialogs --disable-infobars \
  --kiosk --touch-events=enabled \
  "http://localhost:3000/playlists/play/<YOUR_PLAYLIST_UID>?kiosk=true" &
```

Replace `<YOUR_PLAYLIST_UID>` with your playlist's UID (see [Finding the Playlist UID](#finding-the-playlist-uid) below).

**Chromium flags explained:**

| Flag | Effect |
|---|---|
| `--noerrdialogs` | Suppresses crash/error popup dialogs |
| `--disable-infobars` | Removes the "Chrome is being controlled…" banner |
| `--kiosk` | Full-screen mode, no address bar, no window chrome |
| `--touch-events=enabled` | Enables touch input for the 7" display |

---

### Step 3 — Enable Grafana Anonymous Access

By default Grafana requires a login. For a kiosk display you want it to open without any credentials. Add two environment variables to the Grafana service in `docker/docker-compose.yml`:

```yaml
grafana:
  image: grafana/grafana:latest
  environment:
    - GF_SECURITY_ADMIN_USER=admin
    - GF_SECURITY_ADMIN_PASSWORD=admin
    - GF_AUTH_ANONYMOUS_ENABLED=true        # ← add this
    - GF_AUTH_ANONYMOUS_ORG_ROLE=Viewer     # ← add this
```

Then recreate the container to apply:

```bash
cd ~/weather-station/docker
docker compose up -d grafana
```

Verify it works without credentials:

```bash
curl http://localhost:3000/api/playlists
# Should return JSON without asking for a password
```

---

### Step 4 — Auto-Login and Start X on Boot

**4a. Enable console autologin via raspi-config:**

```bash
sudo raspi-config
# → System Options → Boot / Auto Login → Console Autologin
```

Or non-interactively:

```bash
sudo raspi-config nonint do_boot_behaviour B2
```

This makes the Pi log in as the `pi` user automatically on boot instead of showing a login prompt.

**4b. Tell the shell to start X automatically:**

Add the following line to `~/.bash_profile` so that X launches when the Pi logs in on virtual terminal 1:

```bash
echo '[[ -z $DISPLAY && $XDG_VTNR -eq 1 ]] && startx' >> ~/.bash_profile
```

**What this does:**
- `$DISPLAY` is empty → X is not already running
- `$XDG_VTNR -eq 1` → we are on the primary virtual terminal (the one autologin uses)
- If both are true, `startx` is called which reads `~/.config/openbox/autostart`

---

### Step 5 — Reboot

```bash
sudo reboot
```

Boot sequence:
1. Pi powers on → kernel boots
2. systemd starts getty on tty1 → autologin kicks in as `pi`
3. `.bash_profile` runs → `startx` is called
4. X server starts → Openbox session begins
5. `~/.config/openbox/autostart` runs → Chromium opens the Grafana playlist in fullscreen

---

### Finding the Playlist UID

Query the Grafana API to list all playlists and find the UID:

```bash
# If anonymous access is enabled:
curl http://localhost:3000/api/playlists

# If you still need credentials:
curl -u admin:admin http://localhost:3000/api/playlists
```

Example response:

```json
[
  {
    "uid": "ad6vm6w",
    "name": "DashEins",
    "interval": "1m"
  }
]
```

The kiosk URL is then:

```
http://localhost:3000/playlists/play/ad6vm6w?kiosk=true
```

---

### Display Rotation (Optional)

If the 7" touchscreen is mounted in a non-default orientation, add to `/boot/firmware/config.txt`:

```ini
# 0=normal, 1=90°, 2=180°, 3=270°
display_rotate=0
```

---

### Troubleshooting

| Symptom | Fix |
|---|---|
| Black screen after boot | Check `~/.bash_profile` contains the `startx` line; check `~/.xsession-errors` for X errors |
| Grafana login page appears | Confirm `GF_AUTH_ANONYMOUS_ENABLED=true` is set and container was recreated (`docker compose up -d grafana`) |
| Chromium shows "Restore pages?" dialog | Add `--disable-session-crashed-bubble` to Chromium flags in `autostart` |
| Screen goes blank after a few minutes | Confirm `xset s off`, `xset s noblank`, and `xset -dpms` are all present in `autostart` |
| Touch input not working | Confirm `--touch-events=enabled` flag is in Chromium command; check `xinput list` for the touchscreen device |
| `chromium: command not found` | Run `which chromium-browser` — if found, update the command in `autostart` accordingly |

---

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
