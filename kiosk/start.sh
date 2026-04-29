#!/bin/bash
xrandr --output DSI-1 --primary --mode 800x480 --output HDMI-1 --off --output HDMI-2 --off

xset s off
xset s noblank
xset -dpms

unclutter -idle 0.1 -root &

until curl -s http://localhost:3000/api/health > /dev/null 2>&1; do
  sleep 2
done

while true; do
  chromium --noerrdialogs --disable-infobars \
    --kiosk --touch-events=enabled \
    --user-data-dir=/home/pi/kiosk/chrome-profile \
    "file:///home/pi/kiosk/index.html"
  sleep 3
done
