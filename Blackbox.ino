// =====================================================================
//  FLIGHT BLACK BOX MOTION RECORDER — ESP32
//  BMI160 + OLED SSD1306 + Push Button + Active Buzzer +
//  MicroSD Logging + Web Dashboard + MQTT + FreeRTOS (multi-task)
// =====================================================================
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include "webpage.h"  // <-- keep webpage.h in the same folder

// ===================== Wi-Fi / Web (STA-only) =====================
const char* WIFI_SSID     = "Home alone";
const char* WIFI_PASSWORD = "TOPUP321";

WebServer server(80);

// ===================== MQTT (local/self-hosted broker) =====================
const char* MQTT_HOST   = "192.168.1.4";   // <-- GANTI dengan IP broker Mosquitto kamu
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USER   = "hiu";       // <-- GANTI sesuai broker
const char* MQTT_PASS   = "123";       // <-- GANTI sesuai broker
const char* MQTT_CLIENT_ID = "esp32-blackbox-01";

const char* TOPIC_TELEMETRY = "blackbox/telemetry";
const char* TOPIC_STATUS    = "blackbox/status";
const char* TOPIC_EVENT     = "blackbox/event";

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// ===================== Shared Telemetry (protected by mutex) =====================
struct Telemetry {
  float nowG=0, gyroAbs=0, vibRms=0;
  float roll=0, pitch=0;
  float ax=0, ay=0, az=0, gx=0, gy=0, gz=0;
  float hz=0;
  uint32_t lastEventAgeMs=0;
  uint8_t addr=0x68;
} telem;

SemaphoreHandle_t telemMutex;
SemaphoreHandle_t i2cMutex;   // guards Wire bus (BMI160 + OLED share it)
SemaphoreHandle_t sdMutex;    // guards SD/SPI access

// ===================== OLED =====================
#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire);

// ---------- Layout ----------
static inline int rowY(int i){ return 12 + 10*i; } // rows at 12,22,32,42,52
const int COL1_X = 2;
const int COL2_X = 66;

// ===================== Pins =====================
#define SDA_PIN 21
#define SCL_PIN 22
#define BUTTON_PIN 4     // push-button to GND, using INPUT_PULLUP (sesuai rangkaian)
#define BUZZER_PIN 25    // active buzzer (+)

// ---- MicroSD (VSPI bawaan ESP32) ----
#define SD_CS_PIN   5
#define SD_MOSI_PIN 23
#define SD_MISO_PIN 19
#define SD_SCK_PIN  18

// ===================== BMI160 (register-level) =====================
static const uint8_t BMI160_ADDR_PRIMARY = 0x68;
static const uint8_t BMI160_ADDR_ALT     = 0x69;
static const uint8_t REG_GYRO_START  = 0x0C; // GxL..GzH
static const uint8_t REG_ACCEL_START = 0x12; // AxL..AzH
static const uint8_t REG_CMD         = 0x7E;
static const uint8_t CMD_SOFTRESET   = 0xB6;
static const uint8_t CMD_ACC_NORMAL  = 0x11;
static const uint8_t CMD_GYR_NORMAL  = 0x15;

static const float ACC_LSB_PER_G    = 16384.0f; // ±2g default
static const float GYRO_LSB_PER_DPS = 131.0f;   // ±250 dps default

uint8_t bmi_addr = BMI160_ADDR_PRIMARY;

// ===================== Persistence (NVS) =====================
Preferences prefs;
struct Calib {
  float ax_off=0, ay_off=0, az_off=0;  // g (aim: az ~ +1g at rest)
  float gx_off=0, gy_off=0, gz_off=0;  // dps
} calib;

struct Peaks {
  float maxG=0.0f,   minG=  999.0f;
  float maxDPS=0.0f, minDPS=99999.0f;
  float maxVib=0.0f;
  float lastImpactG=0.0f;
  uint32_t freefalls=0, impacts=0, events=0;
} peaks;

void loadNVS() {
  prefs.begin("bb-rec", true);
  calib.ax_off = prefs.getFloat("ax_off", 0.0f);
  calib.ay_off = prefs.getFloat("ay_off", 0.0f);
  calib.az_off = prefs.getFloat("az_off", 0.0f);
  calib.gx_off = prefs.getFloat("gx_off", 0.0f);
  calib.gy_off = prefs.getFloat("gy_off", 0.0f);
  calib.gz_off = prefs.getFloat("gz_off", 0.0f);
  peaks.maxG      = prefs.getFloat("maxG", 0.0f);
  peaks.minG      = prefs.getFloat("minG", 999.0f);
  peaks.maxDPS    = prefs.getFloat("maxDPS", 0.0f);
  peaks.minDPS    = prefs.getFloat("minDPS", 99999.0f);
  peaks.maxVib    = prefs.getFloat("maxVib", 0.0f);
  peaks.lastImpactG= prefs.getFloat("lastImp", 0.0f);
  peaks.freefalls = prefs.getULong("freefalls", 0);
  peaks.impacts   = prefs.getULong("impacts",   0);
  peaks.events    = prefs.getULong("events",    0);
  prefs.end();
}
void saveNVS() {
  prefs.begin("bb-rec", false);
  prefs.putFloat("ax_off", calib.ax_off);
  prefs.putFloat("ay_off", calib.ay_off);
  prefs.putFloat("az_off", calib.az_off);
  prefs.putFloat("gx_off", calib.gx_off);
  prefs.putFloat("gy_off", calib.gy_off);
  prefs.putFloat("gz_off", calib.gz_off);
  prefs.putFloat("maxG",   peaks.maxG);
  prefs.putFloat("minG",   peaks.minG);
  prefs.putFloat("maxDPS", peaks.maxDPS);
  prefs.putFloat("minDPS", peaks.minDPS);
  prefs.putFloat("maxVib", peaks.maxVib);
  prefs.putFloat("lastImp",peaks.lastImpactG);
  prefs.putULong("freefalls", peaks.freefalls);
  prefs.putULong("impacts",   peaks.impacts);
  prefs.putULong("events",    peaks.events);
  prefs.end();
}
void clearPeaks(){ peaks = Peaks(); saveNVS(); }

// ===================== I2C helpers =====================
bool i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}
bool i2cReadBytes(uint8_t addr, uint8_t startReg, uint8_t len, uint8_t* buf) {
  Wire.beginTransmission(addr);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t got = Wire.requestFrom(addr, len);
  if (got != len) return false;
  for (uint8_t i=0;i<len;i++) buf[i]=Wire.read();
  return true;
}
static inline int16_t to_i16(uint8_t lo, uint8_t hi) {
  return (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
}

// ===================== BMI160 bring-up =====================
bool bmi160Begin() {
  for (uint8_t attempt=0; attempt<2; ++attempt) {
    uint8_t addr = (attempt==0)? BMI160_ADDR_PRIMARY : BMI160_ADDR_ALT;
    if (!i2cWriteReg(addr, REG_CMD, CMD_SOFTRESET)) continue;
    delay(100);
    if (!i2cWriteReg(addr, REG_CMD, CMD_ACC_NORMAL)) continue; delay(50);
    if (!i2cWriteReg(addr, REG_CMD, CMD_GYR_NORMAL)) continue; delay(60);
    uint8_t tmp[6];
    if (!i2cReadBytes(addr, REG_ACCEL_START, 6, tmp)) continue;
    bmi_addr = addr;
    return true;
  }
  return false;
}
bool readAccel_g(float& ax, float& ay, float& az) {
  uint8_t b[6]; if (!i2cReadBytes(bmi_addr, REG_ACCEL_START, 6, b)) return false;
  ax = to_i16(b[0], b[1]) / ACC_LSB_PER_G;
  ay = to_i16(b[2], b[3]) / ACC_LSB_PER_G;
  az = to_i16(b[4], b[5]) / ACC_LSB_PER_G;
  return true;
}
bool readGyro_dps(float& gx, float& gy, float& gz) {
  uint8_t b[6]; if (!i2cReadBytes(bmi_addr, REG_GYRO_START, 6, b)) return false;
  gx = to_i16(b[0], b[1]) / GYRO_LSB_PER_DPS;
  gy = to_i16(b[2], b[3]) / GYRO_LSB_PER_DPS;
  gz = to_i16(b[4], b[5]) / GYRO_LSB_PER_DPS;
  return true;
}

// ===================== Sampling / Filters =====================
static const float SAMPLE_HZ   = 200.0f;
static const uint32_t SAMPLE_US= (uint32_t)(1e6f / SAMPLE_HZ);
float roll_deg=0, pitch_deg=0;

const int VIB_WIN = 64;
float vibBuf[VIB_WIN]; int vibIdx=0; int vibCnt=0;

const int CHART_W = 124; // safe bars between x=2..125 (inclusive)
float chartA[CHART_W]; int chA=0;
float chartG[CHART_W]; int chG=0;
float chartV[CHART_W]; int chV=0;

float measuredHz = 0.0f;
uint32_t fpsCount=0, fpsLastMs=0;

// ===================== Events / Thresholds =====================
const float FREEFALL_G = 0.25f;
const uint32_t FREEFALL_MIN_MS = 30;
const float IMPACT_G  = 2.5f;
bool inFreefall=false;
uint32_t freefallStartMs=0;
uint32_t lastEventMs=0;

// ===================== Recording / SD State =====================
volatile bool recording = false;
volatile bool sdReady   = false;
String currentLogFile = "";
File logFile;
uint32_t logLineCount = 0;

// ===================== Buzzer (non-blocking beep queue) =====================
// Pattern dikodekan sebagai jumlah beep + durasi on/off (ms)
struct BeepRequest { uint8_t count; uint16_t onMs; uint16_t offMs; };
QueueHandle_t beepQueue;

void queueBeep(uint8_t count, uint16_t onMs=80, uint16_t offMs=120) {
  BeepRequest req{count, onMs, offMs};
  xQueueSend(beepQueue, &req, 0);
}

// ===================== Pages =====================
enum Page {
  PAGE_HOME=0,         // Summary (very compact)
  PAGE_ACCEL,          // Accel chart
  PAGE_GYRO,           // Gyro chart
  PAGE_VIB,            // Vib RMS chart
  PAGE_ORIENT,         // Orientation (bubble + numbers)
  PAGE_PEAKS_A,        // MaxG / MinG / MaxVib
  PAGE_PEAKS_B,        // MaxDPS / MinDPS / LastImpactG
  PAGE_EVENTS,         // Freefalls / Impacts / Last event
  PAGE_STATUS,         // WiFi / SD / MQTT / Recording status
  PAGE_DIAG_A,         // Addr/Hz + ax/ay
  PAGE_DIAG_B,         // az + gx/gy/gz + |a|/|w|
  PAGE_COUNT
};
int page = PAGE_HOME;

// ===================== Button =====================
struct ButtonState { bool last=false; uint32_t lastMs=0; bool pressed=false; uint32_t startMs=0; bool longFired=false; } btn;
const uint16_t DEBOUNCE_MS=30, LONGPRESS_MS=1200;

void startRecording();
void stopRecording();

void pollButton(uint32_t nowMs) {
  bool lvl = (digitalRead(BUTTON_PIN) == LOW); // pull-up: LOW = pressed
  if (lvl != btn.last && (nowMs - btn.lastMs) >= DEBOUNCE_MS) {
    btn.last = lvl; btn.lastMs = nowMs;
    if (lvl) {
      btn.pressed = true; btn.startMs = nowMs; btn.longFired = false;
    } else if (btn.pressed) {
      uint32_t dur = nowMs - btn.startMs;
      btn.pressed = false;
      if (dur < LONGPRESS_MS) {
        // SHORT PRESS -> cycle OLED page
        page = (page + 1) % PAGE_COUNT;
      }
      // long press action already fired while held (see below)
    }
  }
  // Detect long-press while still held (fires once)
  if (btn.pressed && !btn.longFired && (nowMs - btn.startMs) >= LONGPRESS_MS) {
    btn.longFired = true;
    // LONG PRESS -> toggle Recording ON/OFF
    if (recording) stopRecording(); else startRecording();
  }
}

// ===================== UI helpers =====================
void header(const char* title) {
  oled.fillRect(0,0,128,10,SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_BLACK);
  oled.setCursor(2,1); oled.print(title);
  oled.setCursor(106,1);
  oled.print(page+1); oled.print('/'); oled.print(PAGE_COUNT);
  oled.setTextColor(SSD1306_WHITE);
}
void stripChart(const char* title, float* buf, int headIdx, float nowVal, float vmax, const char* unit) {
  header(title);
  oled.drawRect(1, 12, 126, 34, SSD1306_WHITE);
  int y0 = 45; // bottom inside
  for (int i=0;i<CHART_W;i++) {
    int idx = (headIdx + i) % CHART_W;
    float vv = buf[idx]; if (vv > vmax) vv = vmax;
    int h = (int)(vv * (32.0f / vmax));
    int x = 2 + i;
    if (h > 0) oled.drawLine(x, y0, x, y0 - h, SSD1306_WHITE);
  }
  oled.setCursor(COL1_X, rowY(4));
  if (unit[0]=='d') { oled.print((int)nowVal); oled.print(unit); }
  else { oled.print(nowVal,1); oled.print(unit); }
}

// ===================== Pages =====================
static inline float clampf(float v, float lo, float hi){ return v<lo?lo:(v>hi?hi:v); }

void pageHome(float gRes, float gyroAbs, float vibRms) {
  header("Summary");
  oled.setCursor(COL1_X, rowY(0)); oled.print("NowG:"); oled.print(gRes,2); oled.print(" g");
  oled.setCursor(COL2_X, rowY(0)); oled.print("|w| :"); oled.print((int)gyroAbs); oled.print(" dps");
  oled.setCursor(COL1_X, rowY(1)); oled.print("Vib :"); oled.print(vibRms,2); oled.print(" g");
  oled.setCursor(COL2_X, rowY(1)); oled.print("Hz  :"); oled.print((int)(measuredHz+0.5f));
  oled.setCursor(COL1_X, rowY(2)); oled.print("MaxG:"); oled.print(peaks.maxG,2);
  oled.setCursor(COL2_X, rowY(2)); oled.print("MaxD:"); oled.print((int)peaks.maxDPS);
  oled.setCursor(COL1_X, rowY(3)); oled.print("MinG:"); oled.print(peaks.minG,2);
  oled.setCursor(COL2_X, rowY(3)); oled.print("MinD:"); oled.print((int)peaks.minDPS);
  oled.setCursor(COL1_X, rowY(4)); oled.print("F:"); oled.print(peaks.freefalls); oled.print(" I:"); oled.print(peaks.impacts);
  oled.setCursor(COL2_X, rowY(4)); oled.print(recording ? "REC" : "---");
}
void pageAccel(float gRes) {
  float v = fabsf(gRes - 1.0f); if (v > 2.0f) v = 2.0f;
  chartA[chA] = v; chA = (chA+1) % CHART_W;
  stripChart("Accel |a|-1g", chartA, chA, v, 2.0f, "g");
}
void pageGyro(float gyroAbs) {
  float v = gyroAbs; if (v > 1200.0f) v = 1200.0f;
  chartG[chG] = v; chG = (chG+1) % CHART_W;
  stripChart("Gyro |w|", chartG, chG, gyroAbs, 1200.0f, "dps");
}
void pageVib(float vibRms) {
  float v = vibRms; if (v > 2.0f) v = 2.0f;
  chartV[chV] = v; chV = (chV+1) % CHART_W;
  stripChart("Vibration RMS", chartV, chV, vibRms, 2.0f, "g");
}
void pageOrient(float gRes) {
  header("Orientation");
  int cx = 24;
  int cy = 29;
  int R  = 14;
  oled.drawCircle(cx, cy, R, SSD1306_WHITE);
  int rx = (int)(clampf(roll_deg, -45, 45)  * (R/45.0f));
  int ry = (int)(clampf(pitch_deg,-45, 45) * (R/45.0f));
  oled.fillCircle(cx+rx, cy-ry, 3, SSD1306_WHITE);
  oled.setCursor(COL2_X-10, rowY(1)); oled.print("Roll :"); oled.print(roll_deg,1);
  oled.setCursor(COL2_X-10, rowY(2)); oled.print("Pitch:"); oled.print(pitch_deg,1);
  oled.setCursor(COL2_X-10, rowY(3)); oled.print("|a|:"); oled.print(gRes,2); oled.print("g");
}
void pagePeaksA() {
  header("Peaks A");
  oled.setCursor(COL1_X, rowY(0)); oled.print("MaxG : "); oled.print(peaks.maxG,2); oled.print(" g");
  oled.setCursor(COL1_X, rowY(1)); oled.print("MinG : "); oled.print(peaks.minG,2); oled.print(" g");
  oled.setCursor(COL1_X, rowY(2)); oled.print("MaxV : "); oled.print(peaks.maxVib,2); oled.print(" g");
}
void pagePeaksB() {
  header("Peaks B");
  oled.setCursor(COL1_X, rowY(0)); oled.print("MaxD : "); oled.print((int)peaks.maxDPS); oled.print(" dps");
  oled.setCursor(COL1_X, rowY(1)); oled.print("MinD : "); oled.print((int)peaks.minDPS); oled.print(" dps");
  oled.setCursor(COL1_X, rowY(2)); oled.print("LastG: "); oled.print(peaks.lastImpactG,2); oled.print(" g");
}
void pageEvents(uint32_t nowMs) {
  header("Events");
  uint32_t ago = (lastEventMs==0)? 0 : (nowMs - lastEventMs);
  oled.setCursor(COL1_X, rowY(0)); oled.print("Freefalls: "); oled.print(peaks.freefalls);
  oled.setCursor(COL1_X, rowY(1)); oled.print("Impacts  : "); oled.print(peaks.impacts);
  oled.setCursor(COL1_X, rowY(2));
  if (lastEventMs==0) { oled.print("Last: None"); }
  else {
    if (ago < 1000) { oled.print("Last: "); oled.print(ago); oled.print(" ms"); }
    else if (ago < 60000) { oled.print("Last: "); oled.print((int)(ago/1000)); oled.print(" s"); }
    else { oled.print("Last: "); oled.print((int)(ago/60000)); oled.print(" min"); }
  }
}
void pageStatus() {
  header("Status");
  oled.setCursor(COL1_X, rowY(0)); oled.print("WiFi : "); oled.print(WiFi.status()==WL_CONNECTED ? "OK" : "NO");
  oled.setCursor(COL2_X, rowY(0)); oled.print("MQTT: "); oled.print(mqttClient.connected() ? "OK" : "NO");
  oled.setCursor(COL1_X, rowY(1)); oled.print("SD   : "); oled.print(sdReady ? "OK" : "NO");
  oled.setCursor(COL2_X, rowY(1)); oled.print("REC : "); oled.print(recording ? "ON" : "OFF");
  oled.setCursor(COL1_X, rowY(2)); oled.print("File : "); oled.print(currentLogFile.length() ? currentLogFile : "-");
  oled.setCursor(COL1_X, rowY(3)); oled.print("Lines: "); oled.print(logLineCount);
  oled.setCursor(COL1_X, rowY(4)); oled.print("IP: "); oled.print(WiFi.localIP());
}
void pageDiagA(float ax, float ay) {
  header("Diag A");
  oled.setCursor(COL1_X, rowY(0)); oled.print("Addr: 0x"); oled.print(bmi_addr, HEX);
  oled.setCursor(COL1_X, rowY(1)); oled.print("Hz  : "); oled.print((int)(measuredHz+0.5f));
  oled.setCursor(COL1_X, rowY(2)); oled.print("ax  : "); oled.print(ax,2); oled.print(" g");
  oled.setCursor(COL1_X, rowY(3)); oled.print("ay  : "); oled.print(ay,2); oled.print(" g");
}
void pageDiagB(float az, float gx, float gy, float gz, float gRes, float gyroAbs) {
  header("Diag B");
  oled.setCursor(COL1_X, rowY(0)); oled.print("az  : "); oled.print(az,2); oled.print(" g");
  oled.setCursor(COL1_X, rowY(1)); oled.print("gx  : "); oled.print((int)gx);  oled.print(" dps");
  oled.setCursor(COL1_X, rowY(2)); oled.print("gy  : "); oled.print((int)gy);  oled.print(" dps");
  oled.setCursor(COL1_X, rowY(3)); oled.print("gz  : "); oled.print((int)gz);  oled.print(" dps");
  oled.setCursor(COL1_X, rowY(4)); oled.print("|a| : "); oled.print(gRes,2);   oled.print(" g |w|:");
  oled.print((int)gyroAbs);
}

// ===================== SD Card: Black Box logging =====================
bool sdBegin() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) return false;
  if (SD.cardType() == CARD_NONE) return false;
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  return true;
}

// Cari nama file berikutnya: /logs/LOG0001.csv, LOG0002.csv, dst.
String nextLogFileName() {
  for (int i=1; i<=9999; i++) {
    char buf[32];
    snprintf(buf, sizeof(buf), "/logs/LOG%04d.csv", i);
    if (!SD.exists(buf)) return String(buf);
  }
  return "/logs/LOG9999.csv"; // fallback
}

void startRecording() {
  if (!sdReady) { queueBeep(3); return; } // SD error pattern
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    currentLogFile = nextLogFileName();
    logFile = SD.open(currentLogFile, FILE_WRITE);
    if (logFile) {
      logFile.println("timestamp_ms,ax,ay,az,gx,gy,gz,roll,pitch,event,status");
      logFile.flush();
      logLineCount = 0;
      recording = true;
      queueBeep(1); // Recording ON pattern
    } else {
      queueBeep(3); // SD error
    }
    xSemaphoreGive(sdMutex);
  }
}

void stopRecording() {
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (logFile) { logFile.flush(); logFile.close(); }
    xSemaphoreGive(sdMutex);
  }
  recording = false;
  queueBeep(2); // Recording OFF pattern
}

void logToSD(uint32_t ts, float ax, float ay, float az, float gx, float gy, float gz,
             float rollv, float pitchv, const char* eventStr) {
  if (!recording || !sdReady) return;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    if (logFile) {
      logFile.printf("%lu,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%.2f,%.2f,%s,%s\n",
                      (unsigned long)ts, ax, ay, az, gx, gy, gz, rollv, pitchv,
                      eventStr, recording ? "REC" : "IDLE");
      logLineCount++;
      if (logLineCount % 50 == 0) logFile.flush(); // periodic flush, hindari data loss
    }
    xSemaphoreGive(sdMutex);
  }
}

// ===================== MQTT =====================
void mqttReconnect() {
  if (mqttClient.connected()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    mqttClient.publish(TOPIC_STATUS, "online", true);
  }
}

void mqttPublishTelemetry() {
  if (!mqttClient.connected()) return;
  Telemetry t;
  if (xSemaphoreTake(telemMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    t = telem;
    xSemaphoreGive(telemMutex);
  }
  char payload[320];
  snprintf(payload, sizeof(payload),
    "{\"nowG\":%.3f,\"gyroAbs\":%.2f,\"vibRms\":%.3f,\"roll\":%.2f,\"pitch\":%.2f,"
    "\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f,"
    "\"hz\":%.1f,\"recording\":%s}",
    t.nowG, t.gyroAbs, t.vibRms, t.roll, t.pitch,
    t.ax, t.ay, t.az, t.gx, t.gy, t.gz, t.hz,
    recording ? "true" : "false");
  mqttClient.publish(TOPIC_TELEMETRY, payload);
}

void mqttPublishEvent(const char* evt) {
  if (!mqttClient.connected()) return;
  mqttClient.publish(TOPIC_EVENT, evt);
}

// ===================== Web handlers & helpers =====================
String uptimeString() {
  uint32_t ms = millis();
  uint32_t s = ms / 1000;
  uint32_t m = s / 60; s %= 60;
  uint32_t h = m / 60; m %= 60;
  char buf[32];
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
  return String(buf);
}
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}
void handleData() {
  Telemetry t;
  if (xSemaphoreTake(telemMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    t = telem;
    xSemaphoreGive(telemMutex);
  }
  String j = "{";
  j += "\"nowG\":" + String(t.nowG, 4);
  j += ",\"gyroAbs\":" + String(t.gyroAbs, 2);
  j += ",\"vibRms\":" + String(t.vibRms, 4);
  j += ",\"roll\":" + String(t.roll, 3);
  j += ",\"pitch\":" + String(t.pitch, 3);
  j += ",\"ax\":" + String(t.ax, 4);
  j += ",\"ay\":" + String(t.ay, 4);
  j += ",\"az\":" + String(t.az, 4);
  j += ",\"gx\":" + String(t.gx, 2);
  j += ",\"gy\":" + String(t.gy, 2);
  j += ",\"gz\":" + String(t.gz, 2);
  j += ",\"hz\":" + String(t.hz, 1);
  j += ",\"addr\":" + String((unsigned)t.addr);

  j += ",\"peaks\":{";
  j += "\"maxG\":" + String(peaks.maxG,2) + ",";
  j += "\"minG\":" + String(peaks.minG,2) + ",";
  j += "\"maxDPS\":" + String(peaks.maxDPS,0) + ",";
  j += "\"minDPS\":" + String(peaks.minDPS,0) + ",";
  j += "\"maxVib\":" + String(peaks.maxVib,2) + ",";
  j += "\"lastImpactG\":" + String(peaks.lastImpactG,2);
  j += "}";

  j += ",\"counts\":{";
  j += "\"freefalls\":" + String(peaks.freefalls) + ",";
  j += "\"impacts\":" + String(peaks.impacts) + ",";
  j += "\"events\":" + String(peaks.events);
  j += "}";

  j += ",\"system\":{";
  j += "\"wifi\":" + String(WiFi.status()==WL_CONNECTED ? "true":"false") + ",";
  j += "\"mqtt\":" + String(mqttClient.connected() ? "true":"false") + ",";
  j += "\"sd\":" + String(sdReady ? "true":"false") + ",";
  j += "\"recording\":" + String(recording ? "true":"false") + ",";
  j += "\"logFile\":\"" + currentLogFile + "\",";
  j += "\"logLines\":" + String(logLineCount);
  j += "}";

  uint32_t nowMs = millis();
  uint32_t ageMs = (lastEventMs==0)? 0 : (nowMs - lastEventMs);
  j += ",\"lastEventAgeMs\":" + String(ageMs);
  j += ",\"uptime\":\"" + uptimeString() + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

// Toggle recording dari web dashboard
void handleRecordToggle() {
  if (recording) stopRecording(); else startRecording();
  server.send(200, "application/json", String("{\"recording\":") + (recording?"true":"false") + "}");
}

// Reset peaks dari web dashboard
void handleResetPeaks() {
  clearPeaks();
  server.send(200, "application/json", "{\"ok\":true}");
}

// List file log yang ada di SD (untuk ditampilkan di dashboard)
void handleListLogs() {
  String j = "[";
  bool first = true;
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    File dir = SD.open("/logs");
    if (dir) {
      File f = dir.openNextFile();
      while (f) {
        if (!f.isDirectory()) {
          if (!first) j += ",";
          j += "{\"name\":\"" + String(f.name()) + "\",\"size\":" + String(f.size()) + "}";
          first = false;
        }
        f = dir.openNextFile();
      }
      dir.close();
    }
    xSemaphoreGive(sdMutex);
  }
  j += "]";
  server.send(200, "application/json", j);
}

// Download file CSV: /download?file=LOG0001.csv
void handleDownload() {
  if (!server.hasArg("file")) { server.send(400, "text/plain", "Missing 'file' param"); return; }
  String fname = "/logs/" + server.arg("file");
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (!SD.exists(fname)) {
      xSemaphoreGive(sdMutex);
      server.send(404, "text/plain", "File not found");
      return;
    }
    File f = SD.open(fname, FILE_READ);
    server.sendHeader("Content-Disposition", "attachment; filename=" + server.arg("file"));
    server.streamFile(f, "text/csv");
    f.close();
    xSemaphoreGive(sdMutex);
  } else {
    server.send(503, "text/plain", "SD busy, try again");
  }
}

// ===================== FreeRTOS Tasks =====================
// Task 1 (Core 1): baca sensor BMI160, hitung roll/pitch, update telemetry,
//                  tulis OLED, logging SD, deteksi event. Jalan @200Hz.
void TaskSensor(void* pv) {
  uint32_t nextSample = micros();
  for (;;) {
    uint32_t now = micros();
    if ((int32_t)(now - nextSample) < 0) {
      vTaskDelay(1);
      continue;
    }
    nextSample += SAMPLE_US;

    float ax, ay, az, gx, gy, gz;
    bool ok = false;
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      ok = readAccel_g(ax, ay, az) && readGyro_dps(gx, gy, gz);
      xSemaphoreGive(i2cMutex);
    }
    if (!ok) { vTaskDelay(1); continue; }

    // Apply calibration
    ax -= calib.ax_off; ay -= calib.ay_off; az -= calib.az_off;
    gx -= calib.gx_off; gy -= calib.gy_off; gz -= calib.gz_off;

    float gRes    = sqrtf(ax*ax + ay*ay + az*az);
    float gyroAbs = sqrtf(gx*gx + gy*gy + gz*gz);

    // Complementary filter
    float dt = SAMPLE_US / 1e6f;
    float rollAcc  = atan2f(ay, az) * 180.0f / PI;
    float pitchAcc = atan2f(-ax, sqrtf(ay*ay + az*az)) * 180.0f / PI;
    roll_deg  = 0.98f*(roll_deg  + gx*dt) + 0.02f*rollAcc;
    pitch_deg = 0.98f*(pitch_deg + gy*dt) + 0.02f*pitchAcc;

    // Vibration RMS (|a|-1g)
    float hp = gRes - 1.0f;
    vibBuf[vibIdx] = hp*hp; vibIdx = (vibIdx+1) % VIB_WIN; if (vibCnt < VIB_WIN) vibCnt++;
    float vibSum=0.0f; for (int i=0;i<vibCnt;i++) vibSum += vibBuf[i];
    float vibRms = sqrtf(vibSum / (float)(vibCnt>0?vibCnt:1));

    // Peaks/mins
    bool changed=false;
    if (gRes    > peaks.maxG)    { peaks.maxG = gRes; changed=true; }
    if (gRes    < peaks.minG)    { peaks.minG = gRes; changed=true; }
    if (gyroAbs > peaks.maxDPS)  { peaks.maxDPS = gyroAbs; changed=true; }
    if (gyroAbs < peaks.minDPS)  { peaks.minDPS = gyroAbs; changed=true; }
    if (vibRms  > peaks.maxVib)  { peaks.maxVib = vibRms; changed=true; }
    if (changed) saveNVS();

    // Events
    uint32_t nowMs = millis();
    const char* eventStr = "-";
    if (!inFreefall && gRes < FREEFALL_G) { inFreefall = true; freefallStartMs = nowMs; }
    else if (inFreefall && gRes >= FREEFALL_G) {
      if (nowMs - freefallStartMs >= FREEFALL_MIN_MS) {
        peaks.freefalls++; peaks.events++; saveNVS(); lastEventMs = nowMs;
        eventStr = "FREEFALL";
        queueBeep(2, 60, 60);
        mqttPublishEvent("freefall");
      }
      inFreefall = false;
    }
    if (gRes >= IMPACT_G) {
      peaks.impacts++; peaks.events++; peaks.lastImpactG = gRes; saveNVS(); lastEventMs = nowMs;
      eventStr = "IMPACT";
      queueBeep(3, 60, 60);
      mqttPublishEvent("impact");
    }

    // Charts (bounded width/height)
    float aDisp = fabsf(gRes - 1.0f); if (aDisp > 2.0f) aDisp = 2.0f;
    chartA[chA] = aDisp; chA = (chA+1) % CHART_W;

    float gDisp = gyroAbs; if (gDisp > 1200.0f) gDisp = 1200.0f;
    chartG[chG] = gDisp; chG = (chG+1) % CHART_W;

    float vDisp = vibRms; if (vDisp > 2.0f) vDisp = 2.0f;
    chartV[chV] = vDisp; chV = (chV+1) % CHART_W;

    // Measured loop rate
    fpsCount++;
    uint32_t ms = millis();
    if (ms - fpsLastMs >= 1000) {
      measuredHz = (float)fpsCount * 1000.0f / (float)(ms - fpsLastMs);
      fpsCount = 0; fpsLastMs = ms;
    }

    // Update shared telemetry snapshot
    if (xSemaphoreTake(telemMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      telem.nowG = gRes;
      telem.gyroAbs = gyroAbs;
      telem.vibRms = vibRms;
      telem.roll = roll_deg;
      telem.pitch = pitch_deg;
      telem.ax = ax; telem.ay = ay; telem.az = az;
      telem.gx = gx; telem.gy = gy; telem.gz = gz;
      telem.hz = measuredHz;
      telem.addr = bmi_addr;
      telem.lastEventAgeMs = (lastEventMs==0)? 0 : (ms - lastEventMs);
      xSemaphoreGive(telemMutex);
    }

    // Log ke SD (hanya kalau recording aktif)
    logToSD(ms, ax, ay, az, gx, gy, gz, roll_deg, pitch_deg, eventStr);

    // Gambar OLED (di-throttle agar tidak membebani I2C @200Hz)
    static uint32_t lastDraw = 0;
    if (ms - lastDraw >= 50) { // ~20 FPS OLED, cukup untuk mata manusia
      lastDraw = ms;
      if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        oled.clearDisplay();
        switch (page) {
          case PAGE_HOME:    pageHome(gRes, gyroAbs, vibRms);                    break;
          case PAGE_ACCEL:   pageAccel(gRes);                                    break;
          case PAGE_GYRO:    pageGyro(gyroAbs);                                  break;
          case PAGE_VIB:     pageVib(vibRms);                                    break;
          case PAGE_ORIENT:  pageOrient(gRes);                                   break;
          case PAGE_PEAKS_A: pagePeaksA();                                       break;
          case PAGE_PEAKS_B: pagePeaksB();                                       break;
          case PAGE_EVENTS:  pageEvents(ms);                                     break;
          case PAGE_STATUS:  pageStatus();                                       break;
          case PAGE_DIAG_A:  pageDiagA(ax, ay);                                  break;
          case PAGE_DIAG_B:  pageDiagB(az, gx, gy, gz, gRes, gyroAbs);           break;
        }
        oled.display();
        xSemaphoreGive(i2cMutex);
      }
    }
  }
}

// Task 2 (Core 0): Web server + MQTT + button polling + buzzer.
// Task ini ringan & non-blocking, jalan terus menerus.
void TaskNetwork(void* pv) {
  uint32_t lastMqttPublish = 0;
  uint32_t lastMqttRetry = 0;
  for (;;) {
    uint32_t nowMs = millis();

    pollButton(nowMs);
    server.handleClient();

    if (WiFi.status() == WL_CONNECTED) {
      if (!mqttClient.connected() && (nowMs - lastMqttRetry > 5000)) {
        lastMqttRetry = nowMs;
        mqttReconnect();
      }
      mqttClient.loop();
      if (nowMs - lastMqttPublish >= 1000) { // publish 1Hz
        lastMqttPublish = nowMs;
        mqttPublishTelemetry();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Task 3 (Core 0): Buzzer beeper, mengeksekusi antrian beep tanpa blocking task lain.
void TaskBuzzer(void* pv) {
  BeepRequest req;
  for (;;) {
    if (xQueueReceive(beepQueue, &req, portMAX_DELAY) == pdTRUE) {
      for (uint8_t i=0; i<req.count; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(req.onMs));
        digitalWrite(BUZZER_PIN, LOW);
        if (i < req.count-1) vTaskDelay(pdMS_TO_TICKS(req.offMs));
      }
    }
  }
}

// ===================== Setup =====================
void runCalibration(); // forward

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  i2cMutex    = xSemaphoreCreateMutex();
  telemMutex  = xSemaphoreCreateMutex();
  sdMutex     = xSemaphoreCreateMutex();
  beepQueue   = xQueueCreate(8, sizeof(BeepRequest));

  Wire.begin(SDA_PIN, SCL_PIN, 400000);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) { /* headless ok */ }
  else {
    oled.clearDisplay();
    header("Black-Box Boot");
    oled.setCursor(COL1_X, rowY(2)); oled.print("Init sensors...");
    oled.display();
  }

  loadNVS();

  bool imuOk = bmi160Begin();
  if (!imuOk) {
    oled.clearDisplay(); header("IMU Fail");
    oled.setCursor(COL1_X, rowY(2)); oled.print("Check wiring 0x68/0x69");
    oled.display();
    queueBeep(3); // sensor error pattern
  }

  // Hold button at boot to calibrate
  if (digitalRead(BUTTON_PIN) == LOW) runCalibration();

  for (int i=0;i<VIB_WIN;i++)  vibBuf[i]=0;
  for (int i=0;i<CHART_W;i++) { chartA[i]=0; chartG[i]=0; chartV[i]=0; }

  fpsLastMs = millis();

  // ---- SD Card bring-up ----
  sdReady = sdBegin();
  if (!sdReady) {
    Serial.println("SD Card init FAILED");
    queueBeep(3);
  } else {
    Serial.println("SD Card OK");
  }

  // ---- Wi-Fi bring-up (STA only) ----
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected (STA only). Server still starts.");
  }

  // ---- MQTT bring-up ----
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  if (WiFi.status() == WL_CONNECTED) mqttReconnect();

  // ---- HTTP routes ----
  server.on("/", handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/record/toggle", HTTP_POST, handleRecordToggle);
  server.on("/peaks/reset", HTTP_POST, handleResetPeaks);
  server.on("/logs", HTTP_GET, handleListLogs);
  server.on("/download", HTTP_GET, handleDownload);
  server.begin();
  Serial.println("HTTP server started");

  // Boot success beep
  if (imuOk) queueBeep(1);

  // ---- FreeRTOS Tasks ----
  // Core 1: sensor + OLED + SD logging (time-critical, 200Hz loop)
  xTaskCreatePinnedToCore(TaskSensor,  "TaskSensor",  8192, NULL, 2, NULL, 1);
  // Core 0: networking (WiFi/HTTP/MQTT) + button polling
  xTaskCreatePinnedToCore(TaskNetwork, "TaskNetwork", 8192, NULL, 1, NULL, 0);
  // Core 0: buzzer (low priority, event-driven via queue)
  xTaskCreatePinnedToCore(TaskBuzzer,  "TaskBuzzer",  2048, NULL, 1, NULL, 0);
}

void loop() {
  // Semua kerja dilakukan di FreeRTOS task (TaskSensor / TaskNetwork / TaskBuzzer).
  vTaskDelay(pdMS_TO_TICKS(1000));
}

// ===================== Calibration (hold button at boot) =====================
void runCalibration() {
  oled.clearDisplay();
  oled.fillRect(0,0,128,10,SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK); oled.setTextSize(1);
  oled.setCursor(2,1); oled.print("Calibrating...");
  oled.setTextColor(SSD1306_WHITE);

  // Progress bar frame in row 1
  oled.drawRect(2, rowY(1)-2, 124, 8, SSD1306_WHITE);
  oled.display();

  const int N=200;
  float sax=0, say=0, saz=0, sgx=0, sgy=0, sgz=0;
  for (int i=0;i<N;i++) {
    float ax, ay, az, gx, gy, gz;
    if (!readAccel_g(ax,ay,az)) { i--; continue; }
    if (!readGyro_dps(gx,gy,gz)) { i--; continue; }
    sax+=ax; say+=ay; saz+=az;
    sgx+=gx; sgy+=gy; sgz+=gz;
    int w = (i+1) * 122 / N;
    oled.fillRect(3, rowY(1)-1, w, 6, SSD1306_WHITE);
    oled.display();
    delay(3);
  }
  float axm=sax/N, aym=say/N, azm=saz/N;
  float gxm=sgx/N, gym=sgy/N, gzm=sgz/N;

  calib.ax_off = axm;
  calib.ay_off = aym;
  calib.az_off = azm - 1.0f;   // want +1g on Z at rest
  calib.gx_off = gxm;
  calib.gy_off = gym;
  calib.gz_off = gzm;
  saveNVS();

  oled.clearDisplay();
  oled.fillRect(0,0,128,10,SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK); oled.setCursor(2,1); oled.print("Calib Done");
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(COL1_X, rowY(1)); oled.printf("ax %.3f  ay %.3f", calib.ax_off, calib.ay_off);
  oled.setCursor(COL1_X, rowY(2)); oled.printf("az %.3f", calib.az_off);
  oled.setCursor(COL1_X, rowY(3)); oled.printf("gx %.2f  gy %.2f", calib.gx_off, calib.gy_off);
  oled.setCursor(COL1_X, rowY(4)); oled.printf("gz %.2f", calib.gz_off);
  oled.display();
  delay(800);
}
