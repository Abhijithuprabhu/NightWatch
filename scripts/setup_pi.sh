#!/bin/bash
# ============================================================
#  NightWatch — Raspberry Pi 4 Setup Script
#  Run once as root:  sudo bash setup_pi.sh
#
#  What this does:
#   1. Installs Python deps (Flask, Flask-SocketIO, pyserial, eventlet)
#   2. Copies NightWatch files to /opt/nightwatch
#   3. Adds pi user to dialout group (serial port access)
#   4. Configures Pi as a WiFi hotspot (192.168.4.1)
#      SSID: NightWatch  Password: nightwatch123
#   5. Creates systemd service → auto-starts on boot
#
#  After running:
#   - Power cycle the Pi
#   - Connect phone/laptop to WiFi "NightWatch"
#   - Open browser → http://192.168.4.1:5000
# ============================================================

set -e   # exit on any error

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

# Must be root
[ "$EUID" -eq 0 ] || error "Please run as root: sudo bash setup_pi.sh"

# ── Detect script location ────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="/opt/nightwatch"

# ── Hotspot config ────────────────────────────────────────────
HOTSPOT_SSID="NightWatch"
HOTSPOT_PASS="nightwatch123"
HOTSPOT_IP="192.168.4.1"
HOTSPOT_RANGE_START="192.168.4.10"
HOTSPOT_RANGE_END="192.168.4.50"

info "=== NightWatch Pi Setup ==="
info "App source: $SCRIPT_DIR"
info "App target: $APP_DIR"
info "Hotspot SSID: $HOTSPOT_SSID / Password: $HOTSPOT_PASS"
echo ""

# ── 1. System update + install packages ──────────────────────
info "Step 1/5 — Installing system packages…"
apt-get update -qq
apt-get install -y -qq \
    python3 python3-pip python3-venv \
    hostapd dnsmasq \
    rfkill
info "System packages installed"

# ── 2. Python virtual environment + deps ─────────────────────
info "Step 2/5 — Setting up Python environment…"
mkdir -p "$APP_DIR"
python3 -m venv "$APP_DIR/venv"
"$APP_DIR/venv/bin/pip" install --quiet --upgrade pip
"$APP_DIR/venv/bin/pip" install --quiet \
    flask \
    flask-socketio \
    pyserial \
    eventlet
info "Python deps installed in $APP_DIR/venv"

# ── 3. Copy application files ─────────────────────────────────
info "Step 3/5 — Copying NightWatch files to $APP_DIR…"
cp "$SCRIPT_DIR/app.py"                  "$APP_DIR/app.py"
cp -r "$SCRIPT_DIR/templates"            "$APP_DIR/templates"
cp -r "$SCRIPT_DIR/static"               "$APP_DIR/static" 2>/dev/null || true
chown -R pi:pi "$APP_DIR"

# Add pi user to dialout for serial port access
usermod -aG dialout pi
info "Files copied, pi added to dialout group"

# ── 4. Configure WiFi Hotspot ─────────────────────────────────
info "Step 4/5 — Configuring WiFi hotspot ($HOTSPOT_SSID)…"

# Unblock WiFi radio in case rfkill has it blocked
rfkill unblock wifi || true

# Stop services while we configure
systemctl stop hostapd 2>/dev/null || true
systemctl stop dnsmasq 2>/dev/null || true

# hostapd config
cat > /etc/hostapd/hostapd.conf << HAEOF
interface=wlan0
driver=nl80211
ssid=$HOTSPOT_SSID
hw_mode=g
channel=7
wmm_enabled=0
macaddr_acl=0
auth_algs=1
ignore_broadcast_ssid=0
wpa=2
wpa_passphrase=$HOTSPOT_PASS
wpa_key_mgmt=WPA-PSK
wpa_pairwise=TKIP
rsn_pairwise=CCMP
HAEOF

# Point hostapd to its config
sed -i 's|#DAEMON_CONF=.*|DAEMON_CONF="/etc/hostapd/hostapd.conf"|' \
    /etc/default/hostapd 2>/dev/null || \
    echo 'DAEMON_CONF="/etc/hostapd/hostapd.conf"' >> /etc/default/hostapd

# dnsmasq config — DHCP + DNS for the hotspot subnet
# Back up original if it exists
[ -f /etc/dnsmasq.conf ] && cp /etc/dnsmasq.conf /etc/dnsmasq.conf.bak

cat > /etc/dnsmasq.conf << DMEOF
interface=wlan0
dhcp-range=$HOTSPOT_RANGE_START,$HOTSPOT_RANGE_END,255.255.255.0,24h
domain=local
address=/nightwatch.local/$HOTSPOT_IP
DMEOF

# Static IP for wlan0
# Using dhcpcd to assign static IP on wlan0
DHCPCD=/etc/dhcpcd.conf
if ! grep -q "interface wlan0" "$DHCPCD" 2>/dev/null; then
    cat >> "$DHCPCD" << DHEOF

# NightWatch hotspot static IP
interface wlan0
    static ip_address=$HOTSPOT_IP/24
    nohook wpa_supplicant
DHEOF
    info "Static IP $HOTSPOT_IP/24 added to dhcpcd.conf"
else
    warn "wlan0 already in dhcpcd.conf — skipping static IP block"
fi

# Enable and start
systemctl unmask hostapd 2>/dev/null || true
systemctl enable hostapd
systemctl enable dnsmasq
info "Hotspot configured — will be active after reboot"

# ── 5. Systemd service for NightWatch ────────────────────────
info "Step 5/5 — Creating systemd service…"

cat > /etc/systemd/system/nightwatch.service << SVCEOF
[Unit]
Description=NightWatch Care Monitoring Dashboard
After=network.target multi-user.target
Wants=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=$APP_DIR
ExecStart=$APP_DIR/venv/bin/python app.py
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=multi-user.target
SVCEOF

systemctl daemon-reload
systemctl enable nightwatch
info "nightwatch.service created and enabled"

# ── Done ──────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}  NightWatch setup complete!${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""
echo "  Next steps:"
echo "   1. Plug the receiver ESP32 into the Pi USB port"
echo "   2. Reboot the Pi:  sudo reboot"
echo "   3. Connect to WiFi: '$HOTSPOT_SSID' / '$HOTSPOT_PASS'"
echo "   4. Open browser:   http://$HOTSPOT_IP:5000"
echo "   5. Or use:         http://nightwatch.local:5000"
echo ""
echo "  Useful commands on the Pi:"
echo "   sudo systemctl status nightwatch    — check if running"
echo "   sudo journalctl -u nightwatch -f    — live logs"
echo "   sudo systemctl restart nightwatch   — restart app"
echo ""
echo "  Serial port override:"
echo "   sudo SERIAL_PORT=/dev/ttyACM0 systemctl restart nightwatch"
echo ""
