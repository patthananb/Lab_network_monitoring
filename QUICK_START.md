# Quick Start Guide

This guide will help you get the weather station up and running in about 30 minutes. We provide two deployment options depending on your needs:

- **Headless Setup** — Run everything on a Raspberry Pi with no display. Access Grafana through your web browser from any device on the network.
- **7" Touchscreen Kiosk** — Turn a Raspberry Pi into a dedicated wall-mounted weather display that boots directly into Grafana with no login screen.

Both setups use the same TIG stack (Telegraf, InfluxDB, Grafana) and the same ESP32 sensor firmware. The only difference is how you interact with the data.

## Prerequisites (Both Setups)

- **Raspberry Pi 4** with 2GB+ RAM (4GB recommended for touchscreen)
- **MicroSD card** with Raspberry Pi OS Lite (64-bit)
- **USB-C power supply** (5V 3A minimum)
- **Ethernet or WiFi** connection to your network
- **ESP32-S3** microcontroller with XY-MD02 sensor (flashed separately)

---

# Choose Your Setup

---

## Option A: Headless Setup (No Display)

**Use this if:** You want to run the monitoring stack on a Raspberry Pi and access Grafana from your laptop, phone, or another computer on the network. Perfect for server rooms or closets.

**Time to completion:** ~15 minutes

### Step 1: Prepare the Raspberry Pi

Download [Raspberry Pi Imager](https://www.raspberrypi.com/software/) and flash your MicroSD card with **Raspberry Pi OS Lite (64-bit)**.

Once booted, find your Pi's IP address:
```bash
# From another computer on the network
ping raspberrypi.local
# or check your router's connected devices

# Then SSH in (default password is "raspberry")
ssh pi@<pi-ip-address>
```

Update the system:
```bash
sudo apt update && sudo apt upgrade -y
```

### Step 2: Transfer the Project to Your Pi

From your computer (where you cloned this repo):
```bash
scp -r weather-station/ pi@<pi-ip-address>:~/
```

Then SSH back in:
```bash
ssh pi@<pi-ip-address>
```

### Step 3: Install Docker

Docker makes running the TIG stack simple and reproducible. Install it with:
```bash
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker pi
```

Log out and back in for the Docker group changes to take effect:
```bash
exit
ssh pi@<pi-ip-address>
```

### Step 4: Start the TIG Stack

Navigate to the Docker directory and bring up all services:
```bash
cd ~/weather-station/weatherstation_docker
docker compose up -d
```

Verify they're running:
```bash
docker compose ps
```

You should see **grafana**, **influxdb**, and **mosquitto** containers marked as "Up".

### Step 5: Access Grafana

Open your browser and navigate to:
```
http://<pi-ip-address>:3000
```

Log in with the default credentials:
- **Username:** `admin`
- **Password:** `admin`

You're now in Grafana! It's empty for now, but once your ESP32 starts sending sensor data, it will flow in automatically.

### Step 6: Configure and Upload the Firmware

Now we need to set up the ESP32 to know where to send its data. See the **[Firmware Configuration](#both-setups-firmware-configuration)** section below.

### Managing Services

**View real-time logs:**
```bash
docker compose logs -f grafana
```

**Stop everything:**
```bash
cd ~/weather-station/weatherstation_docker
docker compose down
```

**Start everything again:**
```bash
docker compose up -d
```

### Running Services & Access

Once the stack is running:

| Service | Port | Access | Default Credentials |
|---------|------|--------|-------------------|
| **Grafana** | 3000 | http://pi-ip:3000 | admin / admin |
| **InfluxDB** | 8086 | http://pi-ip:8086 | admin / adminpassword |
| **MQTT** | 1883 | mqtt://pi-ip:1883 | anonymous (no auth) |

> **Note:** See the README.md "Security Note" section for recommended hardening before exposing these services to untrusted networks.

---

## Option B: 7" Touchscreen Kiosk

**Use this if:** You want a beautiful, hands-off weather display for your wall, desk, or dashboard. The Pi boots straight into fullscreen Grafana with zero interaction needed.

**Time to completion:** ~25 minutes

**Why choose this?** No login screens, no browser UI, no mouse cursor. Just clean weather data updating every minute. Perfect for offices, operations centers, or weather enthusiasts.

### Step 1: Prepare the Pi with Display Enabled

Flash your MicroSD card with **Raspberry Pi OS Lite (64-bit)** using [Raspberry Pi Imager](https://www.raspberrypi.com/software/).

Once booted and connected, enable autologin so the Pi logs in automatically without asking for a password:

```bash
ssh pi@<pi-ip-address>
sudo raspi-config
```

Navigate to: **System Options** → **Boot / Auto Login** → **Console Autologin**

Or do it non-interactively:
```bash
sudo raspi-config nonint do_boot_behaviour B2
```

Update the system:
```bash
sudo apt update && sudo apt upgrade -y
```

### Step 2: Install Display Packages

Install a minimal X11 graphical environment with Chromium and Openbox:

```bash
sudo apt install --no-install-recommends \
  xserver-xorg x11-xserver-utils xinit \
  openbox chromium unclutter -y
```

This installs:
- **xserver-xorg** — Display server
- **openbox** — Lightweight window manager (no taskbar)
- **chromium** — Web browser for Grafana
- **unclutter** — Hides mouse cursor when idle

### Step 3: Configure Automatic X Startup

Create a `.bash_profile` that automatically starts X when the Pi boots:

```bash
echo '[[ -z $DISPLAY && $XDG_VTNR -eq 1 ]] && startx' >> ~/.bash_profile
```

This checks: *"If we're not already in X and we're on the main console, run startx."*

### Step 4: Transfer Project and Install Docker

From your computer:
```bash
scp -r weather-station/ pi@<pi-ip-address>:~/
```

SSH back in and install Docker:
```bash
ssh pi@<pi-ip-address>
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
sudo usermod -aG docker pi
exit
ssh pi@<pi-ip-address>
```

### Step 5: Enable Anonymous Grafana Access

For a kiosk, you don't want login screens. Edit the Docker configuration to allow anonymous access:

```bash
nano ~/weather-station/weatherstation_docker/docker-compose.yml
```

Find the `grafana:` section and add these environment variables:

```yaml
grafana:
  image: grafana/grafana:latest
  environment:
    - GF_SECURITY_ADMIN_USER=admin
    - GF_SECURITY_ADMIN_PASSWORD=admin
    - GF_AUTH_ANONYMOUS_ENABLED=true        # ← add this
    - GF_AUTH_ANONYMOUS_ORG_ROLE=Viewer     # ← add this
  ports:
    - "3000:3000"
  # ... rest of config
```

Save: `Ctrl+O`, `Enter`, `Ctrl+X`

### Step 6: Start the TIG Stack

```bash
cd ~/weather-station/weatherstation_docker
docker compose up -d
```

Wait ~10 seconds for Grafana to fully start, then verify:

```bash
curl http://localhost:3000/api/playlists
# Should return JSON without asking for a password
```

### Step 7: Get Your Playlist UID

You'll need a Grafana playlist for the kiosk to display. For now, we'll use the default or create one. Query the API:

```bash
curl http://localhost:3000/api/playlists
```

Example response:
```json
[
  {
    "uid": "abc1234xyz",
    "name": "Weather Dashboard",
    "interval": "1m"
  }
]
```

Copy the `uid` value — you'll need it in the next step.

> **Don't have a dashboard yet?** After your ESP32 sends data, create one in Grafana, then create a playlist from it. Come back and complete this step.

### Step 8: Configure Openbox Autostart

When X starts, Openbox will run a startup script. Create it:

```bash
mkdir -p ~/.config/openbox
nano ~/.config/openbox/autostart
```

Paste this configuration (replace `<YOUR_PLAYLIST_UID>` with the UID from step 7):

```bash
#!/bin/bash

# Disable screen blanking and power saving
xset s off
xset s noblank
xset -dpms

# Hide the mouse cursor after 0.1 seconds of inactivity
unclutter -idle 0.1 -root &

# Launch Chromium in kiosk mode
# Replace <YOUR_PLAYLIST_UID> with the UID from step 7
chromium --noerrdialogs --disable-infobars \
  --kiosk --touch-events=enabled \
  "http://localhost:3000/playlists/play/<YOUR_PLAYLIST_UID>?kiosk=true" &
```

Save: `Ctrl+O`, `Enter`, `Ctrl+X`

**Chromium flags explained:**
- `--noerrdialogs` — Don't show crash dialogs
- `--disable-infobars` — Hide the "Chrome is being controlled" banner
- `--kiosk` — Fullscreen mode, no address bar, no window chrome
- `--touch-events=enabled` — Enable touch input for the 7" display

### Step 9: Configure and Upload Firmware

See the **[Firmware Configuration](#both-setups-firmware-configuration)** section below.

### Step 10: Reboot and Watch It Boot

```bash
sudo reboot
```

**Boot sequence:**
1. Pi powers on
2. Kernel loads
3. Autologin as `pi` user
4. `.bash_profile` runs → `startx` launches X
5. Openbox window manager starts
6. `~/.config/openbox/autostart` runs
7. Chromium opens Grafana playlist in fullscreen
8. Data updates every minute automatically

The first boot may take 30–60 seconds while Grafana initializes. Be patient.

### Display Rotation (Optional)

If your 7" touchscreen is mounted sideways:

```bash
sudo nano /boot/firmware/config.txt
```

Add or modify the `display_rotate` setting:

```ini
# 0=normal, 1=90° clockwise, 2=180°, 3=270° clockwise
display_rotate=0
```

Reboot to apply.

### Troubleshooting

| Problem | Fix |
|---------|-----|
| **Black screen after boot** | SSH in, check `cat ~/.bash_profile` (should contain `startx`), then check `cat ~/.xsession-errors` for X errors. Common: missing packages or `startx` line. |
| **Grafana login page instead of dashboard** | SSH in, verify `GF_AUTH_ANONYMOUS_ENABLED=true` in `docker-compose.yml`, run `docker compose down && docker compose up -d grafana`, wait 10s, reboot Pi. |
| **Chromium shows "Restore pages?" dialog on every boot** | Add this flag to the Chromium command: `--disable-session-crashed-bubble` |
| **Screen blanks after a few minutes** | Verify all three `xset` lines are in `~/.config/openbox/autostart`: `s off`, `s noblank`, `-dpms`. Typos break it. |
| **Touch input not working** | SSH in, run `xinput list` to see if touchscreen is recognized. Verify `--touch-events=enabled` is in the Chromium command in `autostart`. May need to reboot. |
| **Chromium not found** | The package might be `chromium-browser` on older OS versions. SSH in, run `which chromium-browser` and update the command in `autostart` accordingly. |
| **Still stuck?** | SSH in and check logs: `docker compose logs grafana`, `cat ~/.xsession-errors`, `journalctl -xe`. These usually pinpoint the issue. |

---

---

# Firmware Configuration (Both Setups)

Now that your Raspberry Pi is running the TIG stack, you need to configure the ESP32 to send sensor data to it.

### Step 1: Find Your Pi's IP Address

On the Pi, run:
```bash
hostname -I
```

You'll see something like `192.168.1.50`. Write this down — you'll need it.

### Step 2: Configure Credentials

On your computer (not the Pi), open the Arduino IDE and navigate to `firmware/src/firmware/secrets.h`.

Edit these four lines:

```cpp
#define WIFI_SSID "your-wifi-network-name"
#define WIFI_PASS "your-wifi-password"
#define MQTT_BROKER "192.168.1.50"    // ← Use your Pi's IP from step 1
#define MQTT_PORT 1883
```

### Step 3: Install Arduino Libraries

In Arduino IDE:
1. Go to **Sketch** → **Include Library** → **Manage Libraries**
2. Search for `MQTT` and install the one by **256dpi** (usually the first result)
3. Search for `modbus-esp8266` and install the one by **emelianov**

Close the library manager when done.

### Step 4: Select Board and Upload

1. **Tools** → **Board** → Search for `ESP32S3 Dev Module` and select it
2. **Tools** → **USB CDC On Boot** → **Enable**
3. **Tools** → **Port** → Select your ESP32's COM port
4. Click **Upload** (or Ctrl+U)

The upload should complete in ~30 seconds.

### Step 5: Verify Connection

Open **Tools** → **Serial Monitor** and set the baud rate to `115200`.

You should see output like:
```
WiFi connecting...
WiFi connected!
IP: 192.168.1.100
mDNS: esp32s3-weather.local
MQTT connected to 192.168.1.50:1883
```

If you see `MQTT connected`, you're done! The ESP32 is now sending temperature, humidity, and SDM120 power meter data every 2 seconds.

---

# Verifying Everything Works

Once your ESP32 is connected, verify the data is flowing through the stack:

### Check 1: MQTT is Receiving Data

SSH into your Pi and run:
```bash
cd ~/weather-station/weatherstation_docker
docker compose exec mosquitto mosquitto_sub -t "sensors/esp32/data"
```

You should see JSON printed every 2 seconds:
```json
{"status":0,"poll_count":1234,"temperature":24.5,"humidity":48.3,"power_status":0,"power_voltage":229.4,"power_current":0.418,"power_watts":74.6,"power_apparent_va":77.5,"power_reactive_var":12.0,"power_factor":0.963,"power_frequency":50.0,"power_energy_kwh":12.348}
```

Press `Ctrl+C` to stop.

### Check 2: InfluxDB Has Stored the Data

Query the database:
```bash
curl -X POST http://localhost:8086/api/v1/query \
  -H "Authorization: Token mysecrettoken" \
  -d 'org=weatherstation&bucket=sensors' \
  -d 'query=from(bucket:"sensors")|>range(start:-1h)'
```

If successful, you'll see JSON with your sensor measurements.

### Check 3: Grafana Can Access InfluxDB

1. Open your browser to `http://<pi-ip>:3000`
2. Click the **gear icon** (Settings) → **Data Sources**
3. You should see **InfluxDB** listed and marked as "Connected"

If all three checks pass, data is flowing correctly! 🎉

---

# Next Steps

### Create Your First Dashboard

Now that data is flowing, create visualizations in Grafana. See **README.md** for detailed Flux query examples for:
- Temperature trends
- Humidity graphs
- Sensor status
- Pi CPU and memory usage

### For Touchscreen Users: Create a Playlist

Once you have a dashboard you like:
1. In Grafana, click **Dashboards** → **Your New Dashboard**
2. Click **Share** → **Snapshot** or use the dashboard URL
3. Go to **Dashboards** → **Playlists** → **New Playlist**
4. Add your dashboard, set rotation interval (e.g., 1 minute)
5. Save and note the **UID**
6. SSH into your Pi and update the URL in `~/.config/openbox/autostart` with the UID
7. Reboot the Pi

### Security Hardening

Before exposing your system to untrusted networks, see **README.md** → **Security Note** for recommendations on:
- MQTT authentication
- InfluxDB/Grafana password changes
- Network isolation
- TLS encryption

---

# Troubleshooting

### ESP32 not connecting to MQTT

1. **Check WiFi credentials** — Reopen `secrets.h` and verify SSID and password match exactly (case-sensitive)
2. **Check Pi IP address** — Verify `MQTT_BROKER` is set to the correct IP (run `hostname -I` on the Pi to confirm)
3. **Check firewall** — Some routers block port 1883. Verify MQTT is running: `docker compose ps` should show `mosquitto` as "Up"
4. **Serial monitor** — Check the serial output (115200 baud) for error messages

### No data in Grafana

1. **Check MQTT bridge** — Run `docker compose exec mosquitto mosquitto_sub -t "sensors/esp32/data"` — if you see JSON, MQTT is working
2. **Check InfluxDB** — Verify the datasource in Grafana settings is connected
3. **Check Telegraf** — Run `docker compose logs telegraf` for parsing errors
4. **Wait a minute** — Data takes a few seconds to flow through the pipeline

### Grafana login appears on kiosk

This means anonymous access wasn't enabled. On the Pi:
```bash
nano ~/weather-station/weatherstation_docker/docker-compose.yml
```

Add `GF_AUTH_ANONYMOUS_ENABLED=true` and `GF_AUTH_ANONYMOUS_ORG_ROLE=Viewer` to the grafana environment section, then:
```bash
docker compose restart grafana
sudo reboot
```

### Touchscreen not responding to touch

SSH in and run:
```bash
xinput list
```

Look for a device named "Touchscreen" or "ADS7846". If it's not listed, the driver may not be installed — check your Pi's touch display documentation.

---

# Getting Help

- **General questions**: Check **README.md** for architecture details and query examples
- **Hardware issues**: See the Raspberry Pi and ESP32-S3 documentation
- **Docker issues**: Run `docker compose logs <service-name>` to see what went wrong
- **Grafana questions**: Browse the [Grafana documentation](https://grafana.com/docs/) or community forums
