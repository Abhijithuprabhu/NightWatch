// ============================================================
//  NightWatch — mmWave Radar (ESP-NOW version)
//  Sensor: DFRobot SEN0623 Human Detection Sensor
//  Replaces: mmwave_firmware.ino (WiFi/UDP version)
//
//  What changed vs WiFi version:
//   - Removed: WiFi.h, WiFiUdp.h, ssid, password, laptopIP
//   - Added:   esp_now.h — bidirectional with receiver ESP32
//   - Sends data TO receiver every 50ms via ESP-NOW
//   - Receives MODE_FALL / MODE_SLEEP FROM receiver via ESP-NOW
//   - Added:   "src":"MMW" field so receiver can identify us
//   - All radar mode logic is IDENTICAL (unchanged)
//
//  BEFORE FLASHING:
//   Paste your receiver's MAC into RECEIVER_MAC below.
//   Your receiver MAC is: B0:A7:32:81:91:E8
//
//  JSON sent to receiver (every 50ms):
//   Fall mode:  {"src":"MMW","mode":"FALL","presence":1,"fall_state":0}
//   Sleep mode: {"src":"MMW","mode":"SLEEP","presence":1,
//                "sleep_state":1,"respiration":18,"heart":72}
// ============================================================

// esp_now.h MUST be first — before WiFi.h (core 3.x requirement)
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "DFRobot_HumanDetection.h"

// ── Hardware pins ─────────────────────────────────────────────
#define RX_PIN 16
#define TX_PIN 17

HardwareSerial RadarSerial(1);
DFRobot_HumanDetection hu(&RadarSerial);

// ── RECEIVER MAC — paste from receiver Serial Monitor output ──
// Your receiver printed: B0:A7:32:81:91:E8
uint8_t RECEIVER_MAC[] = { 0xB0, 0xA7, 0x32, 0x81, 0x91, 0xE8 };
// ─────────────────────────────────────────────────────────────

bool espNowReady = false;

// ── Radar mode (UNCHANGED logic) ─────────────────────────────
enum RadarMode { MODE_FALL = 1, MODE_SLEEP = 2 };
RadarMode currentMode    = MODE_FALL;
bool      modeSwitching  = false;   // guard: ignore sends while switching

// ── ESP-NOW receive callback — handles mode commands ─────────
// Receiver forwards "MODE_FALL" or "MODE_SLEEP" back to us
void onDataReceive(const esp_now_recv_info_t* info,
                   const uint8_t* data, int len) {
  if (len <= 0 || len > 20) return;

  char cmd[24];
  memcpy(cmd, data, len);
  cmd[len] = '\0';
  String s = String(cmd);
  s.trim();

  Serial.printf("[MMW] Command received via ESP-NOW: %s\n", s.c_str());

  if (s == "MODE_FALL")  setRadarMode(MODE_FALL);
  if (s == "MODE_SLEEP") setRadarMode(MODE_SLEEP);
}

// ── ESP-NOW init ──────────────────────────────────────────────
void initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[MMW] esp_now_init() FAILED — restarting");
    delay(2000);
    ESP.restart();
  }

  // Register receive callback (for mode commands from receiver)
  esp_now_register_recv_cb(onDataReceive);

  // Register receiver as send peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, RECEIVER_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[MMW] Failed to add receiver peer — restarting");
    delay(2000);
    ESP.restart();
  }

  espNowReady = true;

  uint8_t myMac[6];
  esp_wifi_get_mac(WIFI_IF_STA, myMac);
  Serial.printf("[MMW] Own MAC:      %02X:%02X:%02X:%02X:%02X:%02X\n",
                myMac[0],myMac[1],myMac[2],myMac[3],myMac[4],myMac[5]);
  Serial.printf("[MMW] Receiver MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                RECEIVER_MAC[0],RECEIVER_MAC[1],RECEIVER_MAC[2],
                RECEIVER_MAC[3],RECEIVER_MAC[4],RECEIVER_MAC[5]);
  Serial.println("[MMW] ESP-NOW ready (send + receive)");
}

// ── Radar mode switch (IDENTICAL logic, UDP removed) ─────────
void setRadarMode(RadarMode mode) {
  modeSwitching = true;
  Serial.println("[MMW] Switching to mode: " + String(mode));

  if (mode == MODE_FALL) {
    while (hu.configWorkMode(hu.eFallingMode) != 0) delay(500);
    hu.dmInstallHeight(190);
    hu.dmFallConfig(hu.eFallSensitivityC, 2);
    hu.sensorRet();
    currentMode = MODE_FALL;
  }

  if (mode == MODE_SLEEP) {
    while (hu.configWorkMode(hu.eSleepMode) != 0) delay(500);
    hu.sensorRet();
    currentMode = MODE_SLEEP;
  }

  delay(2000);   // allow radar algorithm to stabilise
  modeSwitching = false;
  Serial.println("[MMW] Mode set OK: " + String(currentMode));
}

// ── Send JSON via ESP-NOW (replaces sendUDP) ──────────────────
void sendESPNow(const String& json) {
  if (!espNowReady || json.length() == 0) return;
  esp_err_t result = esp_now_send(RECEIVER_MAC,
                                   (const uint8_t*)json.c_str(),
                                   json.length());
  if (result != ESP_OK) {
    Serial.printf("[MMW] esp_now_send error: %d\n", result);
  }
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  // ESP-NOW first (needs WiFi.mode before radar init)
  initEspNow();

  RadarSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);

  Serial.println("[MMW] Initialising mmWave radar…");
  while (hu.begin() != 0) {
    Serial.println("[MMW] Radar init failed, retrying…");
    delay(1000);
  }
  Serial.println("[MMW] Radar init OK");

  setRadarMode(MODE_FALL);   // default mode on boot
}

// ── Loop ──────────────────────────────────────────────────────
unsigned long lastSend = 0;

void loop() {
  // ESP-NOW receive is handled by the callback — nothing to poll

  if (millis() - lastSend < 50) return;   // 20 Hz send rate
  lastSend = millis();

  // Don't send stale data while switching modes
  if (modeSwitching) return;

  String json;

  if (currentMode == MODE_FALL) {
    uint16_t fallState = hu.getFallData(hu.eFallState);
    uint16_t presence  = hu.smHumanData(hu.eHumanPresence);

    json  = "{";
    json += "\"src\":\"MMW\",";       // ← receiver identification tag
    json += "\"mode\":\"FALL\",";
    json += "\"presence\":"   + String(presence)  + ",";
    json += "\"fall_state\":" + String(fallState);
    json += "}";
  }

  if (currentMode == MODE_SLEEP) {
    uint16_t presence   = hu.smHumanData(hu.eHumanPresence);
    uint8_t  breathe    = hu.getBreatheValue();
    uint8_t  heart      = hu.getHeartRate();
    uint16_t sleepState = hu.smSleepData(hu.eSleepState);

    json  = "{";
    json += "\"src\":\"MMW\",";       // ← receiver identification tag
    json += "\"mode\":\"SLEEP\",";
    json += "\"presence\":"    + String(presence)   + ",";
    json += "\"sleep_state\":" + String(sleepState) + ",";
    json += "\"respiration\":" + String(breathe)    + ",";
    json += "\"heart\":"       + String(heart);
    json += "}";
  }

  // Send via ESP-NOW (replaces sendUDP)
  sendESPNow(json);

  // Keep debug serial output
  Serial.println(json);
}
