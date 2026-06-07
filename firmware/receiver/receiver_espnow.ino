// ============================================================
//  NightWatch — ESP-NOW Receiver
//  Board: ESP32 (any generic — WROOM, WROVER, etc.)
//  Connected to: Raspberry Pi 4 via USB cable
//
//  Responsibilities:
//   1. Receive ESP-NOW packets from UWB+IMU tag and mmWave ESP32
//   2. Forward both data streams as JSON lines to Pi via Serial
//   3. Receive mode commands from Pi over Serial ("MODE_FALL" /
//      "MODE_SLEEP") and relay them to the mmWave ESP32 via ESP-NOW
//
//  Serial format to Pi (115200 baud):
//   UWB data  → {"src":"UWB","A1":...,"A2":...,"roll":...}
//   mmWave    → {"src":"MMW","mode":"FALL","presence":1,...}
//
//  HOW TO GET YOUR RECEIVER'S MAC ADDRESS:
//   Flash this firmware → open Serial Monitor → look for line:
//   [RECEIVER] MAC: XX:XX:XX:XX:XX:XX
//   Paste that MAC into firmware_tag_espnow.ino and
//   firmware_mmwave_espnow.ino as RECEIVER_MAC.
// ============================================================

#include <Arduino.h>
// esp_now.h MUST come before WiFi.h on core 3.x
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

// ── Serial baud rate (must match serial_reader.py on the Pi) ─
#define SERIAL_BAUD 115200

// ── Maximum payload size for ESP-NOW (250 bytes usable) ──────
#define MAX_PAYLOAD 250

// ── How often to flush buffered data to Serial (ms) ──────────
#define FLUSH_INTERVAL_MS 50

// ── Source identification ─────────────────────────────────────
// We distinguish packets by which peer sent them.
// Senders register themselves on first packet — no hard-coding
// of MACs needed here (they're stored when first packet arrives).
// We just need to know which of the two registered peers
// is the UWB tag and which is the mmWave sensor.
//
// Strategy: senders will include a "src" field in their JSON.
// The receiver reads that field to classify them.
// If "src":"UWB" → UWB tag.   If "src":"MMW" → mmWave.
// ────────────────────────────────────────────────────────────

// Peer MAC storage (populated on first packet from each source)
uint8_t uwbMac[6]   = {0};
uint8_t mmwaveMac[6]= {0};
bool    uwbKnown    = false;
bool    mmwaveKnown = false;

// Incoming packet buffers (one per source)
char uwbBuf[MAX_PAYLOAD]   = {0};
char mmwBuf[MAX_PAYLOAD]   = {0};
bool uwbFresh  = false;
bool mmwFresh  = false;

// Timing
unsigned long lastFlush = 0;

// Serial input buffer (for mode commands from Pi)
String serialInputBuf = "";

// ── Helper: inject "src" tag into a JSON string ──────────────
// e.g.  {"A1":1.23,...}  →  {"src":"UWB","A1":1.23,...}
void injectSrcTag(const char* raw, const char* src, char* out, size_t outLen) {
  // raw starts with '{', we insert after the first '{'
  size_t rawLen = strlen(raw);
  if (rawLen < 2 || outLen < rawLen + 14) {
    strncpy(out, raw, outLen - 1);
    out[outLen - 1] = '\0';
    return;
  }
  // Build: {"src":"XXX",<rest of raw after '{'>}
  snprintf(out, outLen, "{\"src\":\"%s\",%s", src, raw + 1);
}

// ── ESP-NOW receive callback ─────────────────────────────────
void onDataReceive(const esp_now_recv_info_t* info,
                   const uint8_t* data, int len) {
  if (len <= 0 || len >= MAX_PAYLOAD) return;

  // Copy raw payload to a null-terminated string
  char raw[MAX_PAYLOAD];
  memcpy(raw, data, len);
  raw[len] = '\0';

  // Detect source by "src" field in the JSON
  bool isUWB   = (strstr(raw, "\"src\":\"UWB\"") != nullptr);
  bool isMMW   = (strstr(raw, "\"src\":\"MMW\"") != nullptr);

  // Also fall back to checking for known UWB-specific fields
  // (in case sender doesn't yet include src tag)
  if (!isUWB && !isMMW) {
    if      (strstr(raw, "\"A1\"")   != nullptr) isUWB = true;
    else if (strstr(raw, "\"mode\"") != nullptr) isMMW = true;
  }

  const uint8_t* mac = info->src_addr;

  if (isUWB) {
    // Register peer MAC if not yet known
    if (!uwbKnown) {
      memcpy(uwbMac, mac, 6);
      uwbKnown = true;
      Serial.printf("[RECEIVER] UWB tag MAC registered: "
                    "%02X:%02X:%02X:%02X:%02X:%02X\n",
                    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    }
    // Store with src tag
    injectSrcTag(raw, "UWB", uwbBuf, sizeof(uwbBuf));
    uwbFresh = true;
  }

  if (isMMW) {
    if (!mmwaveKnown) {
      memcpy(mmwaveMac, mac, 6);
      mmwaveKnown = true;
      Serial.printf("[RECEIVER] mmWave MAC registered: "
                    "%02X:%02X:%02X:%02X:%02X:%02X\n",
                    mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    }
    injectSrcTag(raw, "MMW", mmwBuf, sizeof(mmwBuf));
    mmwFresh = true;
  }
}

// ── ESP-NOW send callback (for mode command confirmation) ─────
// Send callback omitted — return value of esp_now_send() checked directly.
// Avoids send callback signature change in core 3.x vs 2.x.

// ── Forward mode command to mmWave ESP32 via ESP-NOW ─────────
void forwardModeCommand(const String& cmd) {
  if (!mmwaveKnown) {
    Serial.println("[RECEIVER] Cannot forward: mmWave not yet registered");
    return;
  }

  // Ensure peer is registered with esp_now
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, mmwaveMac, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(mmwaveMac)) {
    esp_now_add_peer(&peerInfo);
  }

  const char* payload = cmd.c_str();
  esp_err_t result = esp_now_send(mmwaveMac,
                                   (const uint8_t*)payload,
                                   strlen(payload));
  if (result == ESP_OK) {
    Serial.printf("[RECEIVER] Forwarded to mmWave: %s\n", payload);
  } else {
    Serial.printf("[RECEIVER] esp_now_send error: %d\n", result);
  }
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(500);

  Serial.println("\n[RECEIVER] NightWatch ESP-NOW Receiver starting…");

  // Set WiFi to STA mode (required for ESP-NOW, no AP needed)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Print own MAC — paste this into sender firmwares
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  Serial.printf("[RECEIVER] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  Serial.println("[RECEIVER] Paste the above MAC into:");
  Serial.println("           firmware_tag_espnow.ino → RECEIVER_MAC[]");
  Serial.println("           firmware_mmwave_espnow.ino → RECEIVER_MAC[]");

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("[RECEIVER] FATAL: esp_now_init() failed — restarting");
    delay(2000);
    ESP.restart();
  }

  esp_now_register_recv_cb(onDataReceive);

  Serial.println("[RECEIVER] Ready — listening for ESP-NOW packets\n");
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {

  // ── 1. Read incoming Serial commands from Pi ────────────────
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      serialInputBuf.trim();
      if (serialInputBuf.length() > 0) {
        // Only two valid commands from Pi
        if (serialInputBuf == "MODE_FALL" ||
            serialInputBuf == "MODE_SLEEP") {
          forwardModeCommand(serialInputBuf);
        } else {
          Serial.printf("[RECEIVER] Unknown command from Pi: %s\n",
                        serialInputBuf.c_str());
        }
      }
      serialInputBuf = "";
    } else {
      if (serialInputBuf.length() < 64) {
        serialInputBuf += c;
      }
    }
  }

  // ── 2. Flush buffered sensor data to Pi at fixed interval ───
  unsigned long now = millis();
  if (now - lastFlush >= FLUSH_INTERVAL_MS) {
    lastFlush = now;

    if (uwbFresh) {
      Serial.println(uwbBuf);   // one JSON line for UWB+IMU
      uwbFresh = false;
    }

    if (mmwFresh) {
      Serial.println(mmwBuf);   // one JSON line for mmWave
      mmwFresh = false;
    }
  }
}
