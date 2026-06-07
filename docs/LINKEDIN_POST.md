🚀 Excited to share my Final Year Project — NightWatch, a privacy-preserving elderly care monitoring system built without a single camera or microphone.

The core problem: Most elderly monitoring solutions use cameras, which many elderly people find uncomfortable and intrusive in their own homes. We set out to build something that delivers the same safety assurance — completely camera-free.

🔧 What NightWatch does:
→ Tracks indoor location using UWB (Ultra-Wideband) radar — 2 fixed anchors + 1 wearable tag
→ Detects falls using a 60 GHz mmWave radar (DFRobot C1001) with a 3-state machine: IDLE → IMPACT → POSTFALL
→ Monitors nighttime sleep vitals — respiration rate, heart rate, sleep state
→ Tracks body orientation using a 9-DoF IMU (accelerometer + gyroscope + magnetometer)
→ Streams everything live to a real-time web dashboard on a Raspberry Pi 4

📡 Tech stack that made this interesting:
• ESP-NOW protocol for cable-free, router-free communication between ESP32 nodes
• Median filter (window=5) + exponential smoothing for UWB noise rejection
• Flask + Socket.IO for the real-time dashboard, served directly from the Pi's WiFi hotspot
• systemd service for auto-start on boot — plug in the Pi and it just works

🛠️ Hardware: Vacus ESP32-DW1000 modules, SparkFun ISM330DHCX IMU, SparkFun MMC5983MA magnetometer, Raspberry Pi 4 Model B

The entire codebase — firmware for all 4 ESP32 nodes, the Python backend, and the one-shot Pi setup script — is now open on GitHub (link in comments).

This project pushed me into a lot of territory I hadn't touched before: RF ranging calibration, sensor fusion across multiple buses (SPI + I2C + UART simultaneously), real-time async serial parsing on Linux, and deploying a production-grade embedded system from scratch.

Massive thanks to my teammates Adwait Biju Nair, Ann Maria Francis, Pranav V Manoj, and Sharon Joe Shaji — and our guide Ms. Arya S for the guidance throughout.

If you're working on assistive tech, IoT, or embedded systems — would love to connect and hear what you're building.

#EmbeddedSystems #IoT #ESP32 #UWB #FinalYearProject #APJAKTU #ECE #ElderCare #PrivacyFirst #OpenSource #MakerCommunity #RaspberryPi
