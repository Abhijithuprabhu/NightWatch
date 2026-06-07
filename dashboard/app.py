# ============================================================
#  NightWatch — Raspberry Pi backend (ESP-NOW / Serial version)
#  Replaces: UDP listeners on ports 12345 and 4210
#
#  Data flow:
#   Receiver ESP32 → USB Serial → serial_reader thread
#     → parses JSON lines tagged "src":"UWB" or "src":"MMW"
#     → emits sensor_data / mmwave_data via Socket.IO to browser
#
#  Mode commands (browser → mmWave ESP32):
#   Browser GET /set_mode?mode=MODE_FALL
#     → serial_cmd_queue → serial_writer thread
#     → writes "MODE_FALL\n" to USB Serial
#     → Receiver ESP32 forwards via ESP-NOW to mmWave ESP32
#
#  Serial port auto-detected: first of /dev/ttyUSB0, ttyUSB1,
#  ttyACM0, ttyACM1 that exists.  Override with SERIAL_PORT env var.
# ============================================================

import json
import os
import queue
import glob

from gevent import monkey
monkey.patch_all()   # MUST be first

import serial            # pyserial — installed by setup.sh
from flask import Flask, render_template, request
from flask_socketio import SocketIO

# ── Config ───────────────────────────────────────────────────
BAUD_RATE    = 115200
SERIAL_PORT  = os.environ.get("SERIAL_PORT", "")   # override via env
READ_TIMEOUT = 0.05    # seconds — keeps eventlet responsive

app      = Flask(__name__)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode="gevent")

# Queue for mode commands going OUT to the receiver
serial_cmd_queue = queue.SimpleQueue()

# ── Auto-detect serial port ───────────────────────────────────
def find_serial_port():
    if SERIAL_PORT:
        return SERIAL_PORT
    candidates = [
        "/dev/ttyUSB0", "/dev/ttyUSB1",
        "/dev/ttyACM0", "/dev/ttyACM1",
    ]
    for p in candidates:
        if os.path.exists(p):
            return p
    # Last resort: glob
    found = glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*")
    return found[0] if found else "/dev/ttyUSB0"

# ── Serial reader + writer ────────────────────────────────────
def serial_worker():
    port = find_serial_port()
    print(f"[NightWatch] Opening serial port: {port} @ {BAUD_RATE}")

    while True:
        try:
            ser = serial.Serial(port, BAUD_RATE, timeout=READ_TIMEOUT)
            print(f"[NightWatch] Serial connected: {port}")

            while True:
                # ── Write pending mode commands ──────────────
                try:
                    while True:
                        cmd = serial_cmd_queue.get_nowait()
                        ser.write((cmd + "\n").encode())
                        print(f"[Serial] Sent command: {cmd}")
                except queue.Empty:
                    pass

                # ── Read one line ─────────────────────────────
                raw = ser.readline()
                if not raw:
                    eventlet.sleep(0)   # yield when idle
                    continue

                line = raw.decode("utf-8", errors="ignore").strip()
                if not line or line.startswith("["):
                    # Skip debug lines like [RECEIVER] MAC: ...
                    eventlet.sleep(0)
                    continue

                try:
                    msg = json.loads(line)
                    src = msg.get("src", "")

                    if src == "UWB":
                        socketio.emit("sensor_data", msg)
                    elif src == "MMW":
                        socketio.emit("mmwave_data", msg)
                    else:
                        # Unknown src — try to guess from fields
                        if "A1" in msg:
                            socketio.emit("sensor_data", msg)
                        elif "mode" in msg:
                            socketio.emit("mmwave_data", msg)

                except json.JSONDecodeError:
                    pass   # not a data line — ignore silently
                except Exception as e:
                    print(f"[Serial] Emit error: {e}")

                eventlet.sleep(0)   # yield after each line

        except serial.SerialException as e:
            print(f"[Serial] Connection lost ({e}) — retrying in 3s")
            eventlet.sleep(3)
        except Exception as e:
            print(f"[Serial] Unexpected error ({e}) — retrying in 3s")
            eventlet.sleep(3)

# ── Routes ────────────────────────────────────────────────────
@app.route("/")
def index():
    return render_template("index.html")

@app.route("/set_mode")
def set_mode():
    mode = request.args.get("mode", "")
    if mode not in ("MODE_FALL", "MODE_SLEEP"):
        return "Bad mode", 400
    serial_cmd_queue.put(mode)
    print(f"[NightWatch] Queued mode command: {mode}")
    return "OK"

@app.route("/status")
def status():
    """Quick health-check endpoint — useful for the Pi service."""
    return {"status": "ok", "port": find_serial_port()}

# ── Start ─────────────────────────────────────────────────────
if __name__ == "__main__":
    socketio.start_background_task(serial_worker)
    socketio.run(app, host="0.0.0.0", port=5000, debug=False)
