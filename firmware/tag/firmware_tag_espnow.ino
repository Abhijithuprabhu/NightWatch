// ============================================================
//  NightWatch — UWB Tag + IMU (ESP-NOW version)
//  Replaces: firmware_merged_v2.ino (WiFi/UDP version)
//
//  What changed vs WiFi version:
//   - Removed: WiFi.h, WiFiUdp.h, ssid, password, laptopIP
//   - Added:   esp_now.h — sends directly to receiver ESP32
//   - Added:   "src":"UWB" field so receiver can identify us
//   - All UWB spike filtering is IDENTICAL (unchanged)
//   - All IMU / magnetometer code is IDENTICAL (unchanged)
//
//  BEFORE FLASHING:
//   Paste your receiver's MAC (printed by receiver on boot)
//   into RECEIVER_MAC below.
//   Your receiver MAC is: B0:A7:32:81:91:E8
//
//  JSON sent to receiver via ESP-NOW (every 100 ms):
//  {
//    "src":"UWB",
//    "A1":<dist_m>, "A2":<dist_m>,
//    "rssi1":<dBm>, "rssi2":<dBm>,
//    "roll":<deg>,  "pitch":<deg>, "yaw":<deg>
//  }
// ============================================================

// esp_now.h MUST be first — before WiFi.h and DW1000 (core 3.x requirement)
#include <esp_now.h>
#include <esp_wifi.h>

// ---------- UWB ----------
#include <SPI.h>
#include <DW1000.h>
#include <DW1000Ranging.h>

#define PIN_RST  27
#define PIN_SS    5
#define PIN_IRQ   4
#define ADELAYS  15765
#define TAG_ADDR "7D:00:22:EA:82:60:3B:9C"

String anchor1 = "1783";
String anchor2 = "1784";

// ── Spike filter parameters (tune if needed) ─────────────────
#define BUF_SIZE    5        // median window — odd recommended
#define MAX_RANGE_M 15.0f    // hard ceiling — anything above = junk
#define JUMP_THRESH  3.0f    // max single-step jump allowed (m)
// ─────────────────────────────────────────────────────────────

// Median filter ring buffers
float buf1[BUF_SIZE] = {0};
float buf2[BUF_SIZE] = {0};
int   idx1 = 0, idx2 = 0;
bool  buf1full = false, buf2full = false;

float dist1 = 0.0f, dist2 = 0.0f;
float last1  = 0.0f, last2  = 0.0f;
float rssi1  = 0.0f, rssi2  = 0.0f;

// ── Median filter helpers (UNCHANGED) ────────────────────────
void sortBuf(float* a, int n) {
  for (int i = 0; i < n-1; i++)
    for (int j = i+1; j < n; j++)
      if (a[j] < a[i]) { float t=a[i]; a[i]=a[j]; a[j]=t; }
}

float medianOf(float* buf, int n) {
  float tmp[BUF_SIZE];
  memcpy(tmp, buf, n * sizeof(float));
  sortBuf(tmp, n);
  return tmp[n / 2];
}

void acceptRange(float raw, float* buf, int& idx, bool& full,
                 float& out, float& last, float rssi) {
  if (raw <= 0.0f || raw > MAX_RANGE_M) return;
  if (last > 0.0f && fabsf(raw - last) > JUMP_THRESH) return;
  buf[idx] = raw;
  idx = (idx + 1) % BUF_SIZE;
  if (!full && idx == 0) full = true;
  int count = full ? BUF_SIZE : idx;
  if (count == 0) return;
  out  = medianOf(buf, count);
  last = out;
}

// ---------- IMU / Mag (UNCHANGED) ----------
#include <Wire.h>
#include <SparkFun_ISM330DHCX.h>
#include <SparkFun_MMC5983MA_Arduino_Library.h>

SparkFun_ISM330DHCX imu;
SFE_MMC5983MA       mag;

float roll_deg  = 0.0f;
float pitch_deg = 0.0f;
float yaw_deg   = 0.0f;

// ---------- ESP-NOW (replaces WiFi/UDP) ----------
#include <WiFi.h>

// ── RECEIVER MAC — paste from receiver Serial Monitor output ──
// Your receiver printed: B0:A7:32:81:91:E8
uint8_t RECEIVER_MAC[] = { 0xB0, 0xA7, 0x32, 0x81, 0x91, 0xE8 };
// ─────────────────────────────────────────────────────────────

bool espNowReady = false;

// Send callback omitted — esp_now_send() return value checked directly below.
// This avoids the send callback signature change between core 2.x/3.x.

void initEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("[TAG] esp_now_init() FAILED — retrying in 3s");
    delay(3000);
    ESP.restart();
  }

  // Register receiver as peer
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, RECEIVER_MAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[TAG] Failed to add receiver peer — retrying");
    delay(3000);
    ESP.restart();
  }

  espNowReady = true;

  // Print own MAC for reference
  uint8_t myMac[6];
  esp_wifi_get_mac(WIFI_IF_STA, myMac);
  Serial.printf("[TAG] Own MAC:      %02X:%02X:%02X:%02X:%02X:%02X\n",
                myMac[0],myMac[1],myMac[2],myMac[3],myMac[4],myMac[5]);
  Serial.printf("[TAG] Receiver MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                RECEIVER_MAC[0],RECEIVER_MAC[1],RECEIVER_MAC[2],
                RECEIVER_MAC[3],RECEIVER_MAC[4],RECEIVER_MAC[5]);
  Serial.println("[TAG] ESP-NOW ready");
}

// ---------- UWB callback (UNCHANGED) ----------
void newRange() {
  DW1000Device* device = DW1000Ranging.getDistantDevice();
  String shortAddr = String(device->getShortAddress(), HEX);
  float  range     = device->getRange();
  float  rx        = device->getRXPower();

  if (shortAddr == anchor1) {
    rssi1 = rx;
    acceptRange(range, buf1, idx1, buf1full, dist1, last1, rx);
  }
  if (shortAddr == anchor2) {
    rssi2 = rx;
    acceptRange(range, buf2, idx2, buf2full, dist2, last2, rx);
  }
}

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // --- ESP-NOW (init first — needs WiFi.mode before SPI) ---
  initEspNow();

  // --- UWB ---
  SPI.begin(18, 19, 23, 5);
  DW1000Ranging.initCommunication(PIN_RST, PIN_SS, PIN_IRQ);
  DW1000.setAntennaDelay(ADELAYS);
  DW1000Ranging.attachNewRange(newRange);
  DW1000Ranging.startAsTag(TAG_ADDR, DW1000.MODE_LONGDATA_RANGE_LOWPOWER);

  // --- IMU / Mag on I2C pins 21, 22 (UNCHANGED) ---
  Wire.begin(21, 22);

  if (!imu.begin()) {
    Serial.println("[TAG] IMU not detected!");
    while (1);
  }
  if (!mag.begin()) {
    Serial.println("[TAG] Mag not detected!");
    while (1);
  }

  imu.deviceReset();
  delay(100);
  imu.setDeviceConfig();
  imu.setBlockDataUpdate();
  imu.setAccelDataRate(ISM_XL_ODR_104Hz);
  imu.setAccelFullScale(ISM_4g);
  imu.setGyroDataRate(ISM_GY_ODR_104Hz);
  imu.setGyroFullScale(ISM_500dps);

  Serial.println("[TAG] Ready. Sending every 100ms via ESP-NOW");
  Serial.printf("[TAG] Filter: MAX=%.0fm  JUMP=%.1fm  BUF=%d\n",
                MAX_RANGE_M, JUMP_THRESH, BUF_SIZE);
}

// ---------- IMU update (UNCHANGED) ----------
void updateIMU() {
  if (!imu.checkStatus()) return;

  sfe_ism_data_t accel;
  imu.getAccel(&accel);

  float ax = accel.xData / 1000.0f;
  float ay = accel.yData / 1000.0f;
  float az = accel.zData / 1000.0f;

  float roll  = atan2(ay, az);
  float pitch = atan2(-ax, sqrt(ay * ay + az * az));

  uint32_t mx_raw = mag.getMeasurementX();
  uint32_t my_raw = mag.getMeasurementY();
  uint32_t mz_raw = mag.getMeasurementZ();

  float mx =   (float)mx_raw - 131072.0f;
  float my =   (float)my_raw - 131072.0f;
  float mz = -((float)mz_raw - 131072.0f);

  float mx_comp = mx * cos(pitch) + mz * sin(pitch);
  float my_comp = mx * sin(roll)  * sin(pitch)
                + my * cos(roll)
                - mz * sin(roll)  * cos(pitch);

  float yaw = atan2(-my_comp, mx_comp);

  roll_deg  = roll  * 180.0f / PI;
  pitch_deg = pitch * 180.0f / PI;
  yaw_deg   = yaw   * 180.0f / PI;
}

// ---------- main loop ----------
unsigned long lastSend = 0;

void loop() {
  DW1000Ranging.loop();
  updateIMU();

  if (millis() - lastSend > 100) {
    lastSend = millis();

    // Build JSON — identical fields to WiFi version + "src":"UWB"
    char json[256];
    snprintf(json, sizeof(json),
      "{\"src\":\"UWB\","
       "\"A1\":%.3f,\"A2\":%.3f,"
       "\"rssi1\":%.1f,\"rssi2\":%.1f,"
       "\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f}",
      dist1, dist2,
      rssi1, rssi2,
      roll_deg, pitch_deg, yaw_deg
    );

    // Send via ESP-NOW to receiver (replaces udp.beginPacket / endPacket)
    if (espNowReady) {
      esp_err_t result = esp_now_send(RECEIVER_MAC,
                                      (const uint8_t*)json,
                                      strlen(json));
      if (result != ESP_OK) {
        Serial.printf("[TAG] esp_now_send error: %d\n", result);
      }
    }

    // Keep Serial debug output (useful for bench testing)
    Serial.println(json);
  }
}
