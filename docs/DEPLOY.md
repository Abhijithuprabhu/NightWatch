# NightWatch — Pi Deployment Quick Reference

## File layout on Pi after setup
```
/opt/nightwatch/
├── app.py          ← Flask backend (serial reader)
├── templates/
│   └── index.html  ← NightWatch v5 dashboard
├── static/         ← (empty, for future assets)
├── venv/           ← Python virtual environment
└── requirements.txt
```

## First-time setup
```bash
# 1. Copy this folder to the Pi (USB drive, scp, or git clone)
# 2. On the Pi:
sudo bash setup_pi.sh
sudo reboot
```

## After reboot
- Pi broadcasts WiFi: **NightWatch** / password: **nightwatch123**
- Connect any device to that WiFi
- Open browser → **http://192.168.4.1:5000**
- Or use mDNS → **http://nightwatch.local:5000**

## Service commands
```bash
sudo systemctl status nightwatch     # Is it running?
sudo journalctl -u nightwatch -f     # Live logs (Ctrl+C to exit)
sudo systemctl restart nightwatch    # Restart after file changes
sudo systemctl stop nightwatch       # Stop
```

## Serial port
The app auto-detects /dev/ttyUSB0, ttyUSB1, ttyACM0, ttyACM1.
To override:
```bash
sudo nano /etc/systemd/system/nightwatch.service
# Add under [Service]:
#   Environment=SERIAL_PORT=/dev/ttyACM0
sudo systemctl daemon-reload && sudo systemctl restart nightwatch
```

Check which port the receiver ESP32 appears on:
```bash
ls /dev/ttyUSB* /dev/ttyACM*
# Or watch it appear when you plug it in:
dmesg | tail -20
```

## Hotspot config
Edit `/etc/hostapd/hostapd.conf` to change SSID or password.
Then: `sudo systemctl restart hostapd`

## Update app files only (no full reinstall)
```bash
sudo cp app.py /opt/nightwatch/app.py
sudo cp templates/index.html /opt/nightwatch/templates/index.html
sudo systemctl restart nightwatch
```

## ESP-NOW channel note
All three ESP32s (receiver, tag, mmWave) must use the same
WiFi channel. The Pi hotspot uses channel 7 (set in hostapd.conf).
If the receiver has trouble, check that the ESP32s are also on ch 7.
ESP-NOW uses the channel of the radio's current WiFi mode —
in WIFI_STA mode (which we use) it defaults to ch 1.
If you see packet loss, add this to receiver setup():
  esp_wifi_set_channel(7, WIFI_SECOND_CHAN_NONE);
And the same line to tag and mmWave setup().

## Tested on
- Raspberry Pi 4 Model B 4GB
- Raspberry Pi OS Bookworm (64-bit)
- Python 3.11
