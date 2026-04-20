# Kiosk Display Setup (Raspberry Pi OS Lite + 7" Touchscreen)

This guide covers turning a Raspberry Pi 4 running **Raspberry Pi OS Lite** (headless, no desktop) into a dedicated full-screen Grafana kiosk using a 7" touchscreen display.

## Hardware Requirements
- Raspberry Pi 4 (2 GB or more recommended)
- Raspberry Pi Official 7" Touchscreen Display (800×480)
- MicroSD card with **Raspberry Pi OS Lite (64-bit)** flashed

## Packages Overview
| Package | Purpose |
|---|---|
| `xserver-xorg` | Minimal X11 display server |
| `x11-xserver-utils` | `xset` utility for disabling screen blanking |
| `xinit` | `startx` command to launch X from the console |
| `openbox` | Lightweight window manager (no taskbar, no desktop) |
| `chromium` | Browser for rendering Grafana in kiosk mode |
| `unclutter` | Hides the mouse cursor when idle |

## Installation Steps

### 1. Install Display Packages
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install --no-install-recommends \
  xserver-xorg x11-xserver-utils xinit \
  openbox chromium unclutter -y
```

### 2. Configure Openbox Autostart
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

### 3. Enable Grafana Anonymous Access
Add these environment variables to the `grafana` service in `docker-compose.yml`:
```yaml
environment:
  - GF_AUTH_ANONYMOUS_ENABLED=true
  - GF_AUTH_ANONYMOUS_ORG_ROLE=Viewer
```
Then restart Grafana: `docker compose up -d grafana`.

### 4. Auto-Login and Start X on Boot
Enable console autologin via `raspi-config`:
```bash
sudo raspi-config nonint do_boot_behaviour B2
```
Tell the shell to start X automatically by adding this to `~/.bash_profile`:
```bash
echo '[[ -z $DISPLAY && $XDG_VTNR -eq 1 ]] && startx' >> ~/.bash_profile
```

### 5. Reboot
```bash
sudo reboot
```

## Finding the Playlist UID
Query the Grafana API:
```bash
curl http://localhost:3000/api/playlists
```
Look for the `"uid"` field in the JSON response.
