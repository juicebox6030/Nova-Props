// src/main.cpp
#include <Arduino.h>

#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoOTA.h>

#include <LittleFS.h>
#include <ArduinoJson.h>

#include <ESPAsyncE131.h>
#include <lwip/def.h>   // ntohs

// =====================================================
// ESP32 WROOM-32: sACN -> DC Motor (L298P) + Stepper (ULN2003)
// - sACN E1.31: UNICAST default, optional MULTICAST
// - AP fallback + Web UI + OTA (STA + AP)
// - DC motor: 16-bit center speed (2 slots) => forward/reverse + PWM
// - Stepper: 16-bit position (2 slots) => degrees => steps (ULN2003 half-step)
// - Soft limits (min/max degrees) + manual homing (button + web)
// - Minimal sACN diagnostics + test buttons
//
// IMPORTANT: ArduinoJson v7-safe (no proxy copies).
// =====================================================

// ---------------------------
// Pin map (ESP32 safe pins)
// ---------------------------
// ULN2003 stepper IN1–IN4
static constexpr uint8_t STEP_IN1 = 16;
static constexpr uint8_t STEP_IN2 = 17;
static constexpr uint8_t STEP_IN3 = 18;
static constexpr uint8_t STEP_IN4 = 19;

// L298P Motor Shield (Motor A) control model (per board table):
//   Direction: EA -> Arduino D3  (we wire to ESP32 GPIO below)
//   PWM:       MA -> Arduino D6  (we wire to ESP32 GPIO below)
// This shield uses ONE direction pin + ONE PWM pin for Motor A.
static constexpr uint8_t DC_DIR_PIN = 25;   // ESP32 GPIO -> Shield D3 (EA)
static constexpr uint8_t DC_PWM_PIN = 27;   // ESP32 GPIO -> Shield D6 (MA)

// Manual Home button (to GND)
static constexpr uint8_t HOME_BTN = 23;

// ---------------------------
// PWM config (ESP32 LEDC)
// ---------------------------
static constexpr uint8_t  DC_PWM_CH   = 0;
static constexpr uint32_t DC_PWM_HZ   = 500;   // 
static constexpr uint8_t  DC_PWM_BITS = 8;       // 0..255

// ---------------------------
// sACN
// ---------------------------
ESPAsyncE131 e131(1); // 1 universe buffer

enum SacnMode : uint8_t { SACN_UNICAST = 0, SACN_MULTICAST = 1 };
enum DmxLossMode : uint8_t { LOSS_FORCE_OFF = 0, LOSS_FORCE_ON = 1, LOSS_HOLD_LAST = 2 };
enum DcStopMode : uint8_t { DC_COAST = 0, DC_BRAKE = 1 };

struct AppConfig {
  // WiFi
  String ssid;
  String pass;

  bool useStatic = false;
  IPAddress ip   = IPAddress(192,168,1,60);
  IPAddress gw   = IPAddress(192,168,1,1);
  IPAddress mask = IPAddress(255,255,255,0);

  // sACN
  uint16_t universe = 1;
  uint16_t startAddr = 1;  // 1..510 (we use 2 slots each for DC and Stepper)
  SacnMode sacnMode = SACN_UNICAST;

  // DMX loss
  DmxLossMode lossMode = LOSS_FORCE_OFF;
  uint32_t lossTimeoutMs = 1000;

  // DC motor tuning
  int16_t dcDeadband = 900;    // centered deadband in signed 16-bit space
  uint8_t dcMaxPwm   = 255;    // clamp max speed
  DcStopMode dcStopMode = DC_COAST;

  // Stepper
  uint16_t stepsPerRev = 4096; // half-step steps per output shaft revolution (configurable)
  float maxDegPerSec   = 90.0f;

  // Soft limits
  bool limitsEnabled = false;
  float minDeg = 0.0f;
  float maxDeg = 360.0f;

  // Home offset: step position that corresponds to 0 degrees
  int32_t homeOffsetSteps = 0;
};

static AppConfig cfg;
static const char* CFG_PATH = "/config.json";

// Runtime state
static bool e131Started = false;
static bool haveDmx = false;
static uint32_t lastDmxMs = 0;

// Diagnostics
static uint32_t sacnPacketCount = 0;
static uint16_t lastUniverseSeen = 0;
static uint16_t lastDcRaw16 = 0;
static uint16_t lastStepRaw16 = 0;

// DC motor state
static int16_t dcLastCmd = 0;

// Stepper state
static int32_t stepCurrent = 0;
static int32_t stepTarget  = 0;
static uint8_t halfStepIdx = 0;
static uint32_t nextStepDueUs = 0;

// Home button debounce
static bool lastBtn = true;
static uint32_t btnStableMs = 0;

// Web server
WebServer server(80);

// ---------------------------
// Helpers
// ---------------------------
static String deviceName() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "ESP32-Motion-%04X", (uint16_t)(mac & 0xFFFF));
  return String(buf);
}

static bool parseIp(const String& s, IPAddress& out) {
  int a,b,c,d;
  if (sscanf(s.c_str(), "%d.%d.%d.%d", &a,&b,&c,&d) != 4) return false;
  if (a<0||a>255||b<0||b>255||c<0||c>255||d<0||d>255) return false;
  out = IPAddress((uint8_t)a,(uint8_t)b,(uint8_t)c,(uint8_t)d);
  return true;
}

static void sanity() {
  if (cfg.universe < 1) cfg.universe = 1;
  if (cfg.startAddr < 1) cfg.startAddr = 1;
  if (cfg.startAddr > 508) cfg.startAddr = 508; // need 4 slots total (2 + 2)
  if (cfg.lossTimeoutMs < 100) cfg.lossTimeoutMs = 100;
  if (cfg.lossTimeoutMs > 60000) cfg.lossTimeoutMs = 60000;
  if (cfg.stepsPerRev < 200) cfg.stepsPerRev = 200;
  if (cfg.stepsPerRev > 20000) cfg.stepsPerRev = 20000;
  if (cfg.maxDegPerSec < 1.0f) cfg.maxDegPerSec = 1.0f;
  if (cfg.maxDegPerSec > 720.0f) cfg.maxDegPerSec = 720.0f;
  if (cfg.minDeg > cfg.maxDeg) {
    float t = cfg.minDeg; cfg.minDeg = cfg.maxDeg; cfg.maxDeg = t;
  }
}

// ---------------------------
// Config (ArduinoJson v7-safe)
// ---------------------------
static bool loadConfig() {
  if (!LittleFS.exists(CFG_PATH)) return false;
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  cfg.ssid = doc["wifi"]["ssid"].is<const char*>() ? String(doc["wifi"]["ssid"].as<const char*>()) : String("");
  cfg.pass = doc["wifi"]["pass"].is<const char*>() ? String(doc["wifi"]["pass"].as<const char*>()) : String("");

  cfg.useStatic = doc["wifi"]["static"]["enabled"] | false;

  // Arrays: access via JsonArray (no proxy copies)
  if (doc["wifi"]["static"]["ip"].is<JsonArray>()) {
    JsonArray a = doc["wifi"]["static"]["ip"].as<JsonArray>();
    if (a.size() == 4) cfg.ip = IPAddress((uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], (uint8_t)a[3]);
  }
  if (doc["wifi"]["static"]["gw"].is<JsonArray>()) {
    JsonArray a = doc["wifi"]["static"]["gw"].as<JsonArray>();
    if (a.size() == 4) cfg.gw = IPAddress((uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], (uint8_t)a[3]);
  }
  if (doc["wifi"]["static"]["mask"].is<JsonArray>()) {
    JsonArray a = doc["wifi"]["static"]["mask"].as<JsonArray>();
    if (a.size() == 4) cfg.mask = IPAddress((uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2], (uint8_t)a[3]);
  }

  cfg.universe = doc["dmx"]["universe"] | 1;
  cfg.startAddr = doc["dmx"]["startAddr"] | 1;
  cfg.sacnMode = (SacnMode)((int)doc["dmx"]["sacnMode"] | (int)SACN_UNICAST);

  cfg.lossMode = (DmxLossMode)((int)doc["dmx"]["lossMode"] | (int)LOSS_FORCE_OFF);
  cfg.lossTimeoutMs = doc["dmx"]["lossTimeoutMs"] | 1000;

  cfg.dcDeadband = doc["dc"]["deadband"] | 900;
  cfg.dcMaxPwm = doc["dc"]["maxPwm"] | 255;
  cfg.dcStopMode = (DcStopMode)((int)doc["dc"]["stopMode"] | (int)DC_COAST);

  cfg.stepsPerRev = doc["stepper"]["stepsPerRev"] | 4096;
  cfg.maxDegPerSec = doc["stepper"]["maxDegPerSec"] | 90.0f;
  cfg.homeOffsetSteps = doc["stepper"]["homeOffsetSteps"] | 0;

  cfg.limitsEnabled = doc["stepper"]["limits"]["enabled"] | false;
  cfg.minDeg = doc["stepper"]["limits"]["minDeg"] | 0.0f;
  cfg.maxDeg = doc["stepper"]["limits"]["maxDeg"] | 360.0f;

  sanity();
  return true;
}

static bool saveConfig() {
  JsonDocument doc;

  doc["wifi"]["ssid"] = cfg.ssid;
  doc["wifi"]["pass"] = cfg.pass;
  doc["wifi"]["static"]["enabled"] = cfg.useStatic;

  JsonObject wifiStatic = doc["wifi"]["static"].to<JsonObject>();
  JsonArray ip = wifiStatic.createNestedArray("ip");
  ip.add(cfg.ip[0]); ip.add(cfg.ip[1]); ip.add(cfg.ip[2]); ip.add(cfg.ip[3]);
  JsonArray gw = wifiStatic.createNestedArray("gw");
  gw.add(cfg.gw[0]); gw.add(cfg.gw[1]); gw.add(cfg.gw[2]); gw.add(cfg.gw[3]);
  JsonArray mk = wifiStatic.createNestedArray("mask");
  mk.add(cfg.mask[0]); mk.add(cfg.mask[1]); mk.add(cfg.mask[2]); mk.add(cfg.mask[3]);

  doc["dmx"]["universe"] = cfg.universe;
  doc["dmx"]["startAddr"] = cfg.startAddr;
  doc["dmx"]["sacnMode"] = (int)cfg.sacnMode;

  doc["dmx"]["lossMode"] = (int)cfg.lossMode;
  doc["dmx"]["lossTimeoutMs"] = cfg.lossTimeoutMs;

  doc["dc"]["deadband"] = cfg.dcDeadband;
  doc["dc"]["maxPwm"] = cfg.dcMaxPwm;
  doc["dc"]["stopMode"] = (int)cfg.dcStopMode;

  doc["stepper"]["stepsPerRev"] = cfg.stepsPerRev;
  doc["stepper"]["maxDegPerSec"] = cfg.maxDegPerSec;
  doc["stepper"]["homeOffsetSteps"] = cfg.homeOffsetSteps;
  doc["stepper"]["limits"]["enabled"] = cfg.limitsEnabled;
  doc["stepper"]["limits"]["minDeg"] = cfg.minDeg;
  doc["stepper"]["limits"]["maxDeg"] = cfg.maxDeg;

  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  bool ok = (serializeJson(doc, f) > 0);
  f.close();
  return ok;
}

// ---------------------------
// DC Motor control (L298P Shield Motor A: DIR + PWM)
// ---------------------------
static void dcStop() {
  // On this shield, Motor A stop is achieved by PWM=0.
  // (Classic L298 "brake" by shorting outputs isn't exposed via pins here.)
  ledcWrite(DC_PWM_CH, 0);
}

static void dcApplySigned(int16_t signedCmd) {
  int32_t v = signedCmd;

  if (abs(v) <= cfg.dcDeadband) v = 0;
  dcLastCmd = (int16_t)v;

  if (v == 0) {
    dcStop();
    return;
  }

  bool fwd = (v > 0);
  uint32_t mag = (uint32_t)abs(v);

  uint32_t out = (mag * cfg.dcMaxPwm) / 32768;
  if (out < 1) out = 1;
  if (out > cfg.dcMaxPwm) out = cfg.dcMaxPwm;

  // Direction pin (EA): HIGH=forward, LOW=reverse (per board table)
  digitalWrite(DC_DIR_PIN, fwd ? HIGH : LOW);

  ledcWrite(DC_PWM_CH, (uint8_t)out);
}

// ---------------------------
// Stepper control (ULN2003 half-step)
// ---------------------------
static constexpr uint8_t HALFSEQ[8][4] = {
  {1,0,0,0},
  {1,1,0,0},
  {0,1,0,0},
  {0,1,1,0},
  {0,0,1,0},
  {0,0,1,1},
  {0,0,0,1},
  {1,0,0,1},
};

static void stepperWritePhase(uint8_t idx) {
  digitalWrite(STEP_IN1, HALFSEQ[idx][0] ? HIGH : LOW);
  digitalWrite(STEP_IN2, HALFSEQ[idx][1] ? HIGH : LOW);
  digitalWrite(STEP_IN3, HALFSEQ[idx][2] ? HIGH : LOW);
  digitalWrite(STEP_IN4, HALFSEQ[idx][3] ? HIGH : LOW);
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static int32_t degToSteps(float deg) {
  float d = deg;
  if (cfg.limitsEnabled) d = clampf(d, cfg.minDeg, cfg.maxDeg);
  float stepsPerDeg = (float)cfg.stepsPerRev / 360.0f;
  int32_t steps = (int32_t)lroundf(d * stepsPerDeg);
  return steps + cfg.homeOffsetSteps;
}

static float stepsToDeg(int32_t steps) {
  float stepsPerDeg = (float)cfg.stepsPerRev / 360.0f;
  return (float)(steps - cfg.homeOffsetSteps) / stepsPerDeg;
}

static void stepperSetTargetDeg(float deg) {
  stepTarget = degToSteps(deg);
}

static void stepperTick() {
  if (stepCurrent == stepTarget) return;

  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextStepDueUs) < 0) return;

  // compute step interval based on maxDegPerSec
  float stepsPerDeg = (float)cfg.stepsPerRev / 360.0f;
  float maxStepsPerSec = cfg.maxDegPerSec * stepsPerDeg;
  if (maxStepsPerSec < 1.0f) maxStepsPerSec = 1.0f;

  uint32_t intervalUs = (uint32_t)(1000000.0f / maxStepsPerSec);
  if (intervalUs < 300) intervalUs = 300;

  if (stepTarget > stepCurrent) {
    stepCurrent++;
    halfStepIdx = (halfStepIdx + 1) & 0x07;
  } else {
    stepCurrent--;
    halfStepIdx = (halfStepIdx + 7) & 0x07;
  }

  stepperWritePhase(halfStepIdx);
  nextStepDueUs = nowUs + intervalUs;
}

// ---------------------------
// sACN handling
// ---------------------------
static uint16_t readSlot16(const e131_packet_t& p, uint16_t slotStart1based) {
  // DMX property_values[0]=start code, slots start at [1]
  uint16_t hi = p.property_values[slotStart1based];
  uint16_t lo = p.property_values[slotStart1based + 1];
  return (uint16_t)((hi << 8) | lo);
}

static int16_t u16ToSignedCentered(uint16_t v) {
  // 0..65535 -> -32768..+32767
  return (int16_t)((int32_t)v - 32768);
}

static void applyDmxFromPacket(const e131_packet_t& p) {
  // 4 slots total:
  // startAddr+0..+1 : DC motor 16-bit
  // startAddr+2..+3 : Stepper position 16-bit (0..65535 => 0..360 deg by default)
  uint16_t base = cfg.startAddr;

  uint16_t dcRaw = readSlot16(p, base);
  uint16_t stRaw = readSlot16(p, base + 2);

  lastDcRaw16 = dcRaw;
  lastStepRaw16 = stRaw;

  // DC: signed command
  int16_t signedDc = u16ToSignedCentered(dcRaw);
  dcApplySigned(signedDc);

  // Stepper: map 0..65535 => 0..360 degrees (then limits apply)
  float deg = ((float)stRaw / 65535.0f) * 360.0f;
  stepperSetTargetDeg(deg);

  haveDmx = true;
  lastDmxMs = millis();
}

// ---------------------------
// DMX loss behavior
// ---------------------------
static void enforceDmxLoss() {
  if (!haveDmx) return;
  uint32_t now = millis();
  if ((uint32_t)(now - lastDmxMs) < cfg.lossTimeoutMs) return;

  haveDmx = false;

  switch (cfg.lossMode) {
    case LOSS_FORCE_OFF:
      dcStop();
      // Hold stepper where it is (safe)
      break;
    case LOSS_FORCE_ON:
      // "On" doesn't make much sense for motor; treat as stop for safety
      dcStop();
      break;
    case LOSS_HOLD_LAST:
    default:
      // do nothing
      break;
  }
}

// ---------------------------
// WiFi + AP fallback
// ---------------------------
static bool connectSta(uint32_t timeoutMs) {
  if (cfg.ssid.length() == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);

  if (cfg.useStatic) {
    WiFi.config(cfg.ip, cfg.gw, cfg.mask);
  }

  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    yield();
    if ((uint32_t)(millis() - start) > timeoutMs) break;
  }
  return (WiFi.status() == WL_CONNECTED);
}

static void startAp() {
  WiFi.mode(WIFI_AP);
  String ssid = deviceName();
  WiFi.softAP(ssid.c_str()); // open AP for quick setup
}

// ---------------------------
// OTA
// ---------------------------
static void setupOta() {
  ArduinoOTA.setHostname(deviceName().c_str());
  ArduinoOTA.begin();
}

// ---------------------------
// sACN start
// ---------------------------
static void startSacn() {
  if (cfg.sacnMode == SACN_MULTICAST) {
    e131.begin(E131_MULTICAST, cfg.universe, 1);
  } else {
    // UNICAST mode: listen on universe, accept unicast to our IP
    e131.begin(E131_UNICAST, cfg.universe, 1);
  }
  e131Started = true;
}

// ---------------------------
// Web UI
// ---------------------------
static String htmlHead(const String& title) {
  return String("<!doctype html><html><head><meta charset='utf-8'>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'>"
                "<title>") + title + "</title></head>"
                "<body style='font-family:sans-serif;max-width:820px;margin:16px;'>";
}

static void handleRoot() {
  String s = htmlHead(deviceName());
  s += "<h2>" + deviceName() + "</h2>";
  s += "<p><b>Mode:</b> " + String((WiFi.getMode() & WIFI_AP) ? "AP" : "STA");
  if (WiFi.getMode() & WIFI_STA) s += " | <b>IP:</b> " + WiFi.localIP().toString();
  if (WiFi.getMode() & WIFI_AP)  s += " | <b>AP IP:</b> " + WiFi.softAPIP().toString();
  s += "</p>";

  s += "<h3>sACN Diagnostics</h3>";
  s += "<ul>";
  s += "<li>Universe configured: " + String(cfg.universe) + "</li>";
  s += "<li>Start Addr: " + String(cfg.startAddr) + " (uses 4 slots)</li>";
  s += "<li>Mode: " + String(cfg.sacnMode == SACN_UNICAST ? "UNICAST" : "MULTICAST") + "</li>";
  s += "<li>Packets: " + String(sacnPacketCount) + "</li>";
  s += "<li>Last universe seen: " + String(lastUniverseSeen) + "</li>";
  s += "<li>Last DC raw16: " + String(lastDcRaw16) + "</li>";
  s += "<li>Last Step raw16: " + String(lastStepRaw16) + "</li>";
  s += "<li>DMX active: " + String(haveDmx ? "yes" : "no") + "</li>";
  s += "</ul>";

  s += "<h3>Quick Tests</h3>";
  s += "<p><a href='/test/dc/fwd'>DC FWD</a> | "
       "<a href='/test/dc/rev'>DC REV</a> | "
       "<a href='/test/dc/stop'>DC STOP</a></p>";

  s += "<p><a href='/test/step/0'>Step 0°</a> | "
       "<a href='/test/step/90'>90°</a> | "
       "<a href='/test/step/180'>180°</a> | "
       "<a href='/test/step/270'>270°</a> | "
       "<a href='/test/step/360'>360°</a></p>";

  s += "<p><a href='/home'>Set Home (current as 0°)</a></p>";
  s += "<p><a href='/wifi'>WiFi</a> | <a href='/dmx'>DMX/Motion</a></p>";
  s += "</body></html>";
  server.send(200, "text/html", s);
}

static void handleWifi() {
  String s = htmlHead("WiFi");
  s += "<h2>WiFi Settings</h2>";
  s += "<form method='POST' action='/savewifi'>";
  s += "SSID: <input name='ssid' value='" + cfg.ssid + "'><br><br>";
  s += "Password: <input name='pass' type='password' value='" + cfg.pass + "'><br><br>";
  s += "<label><input name='st' type='checkbox' " + String(cfg.useStatic ? "checked" : "") + "> Static IP</label><br><br>";
  s += "IP: <input name='ip' value='" + cfg.ip.toString() + "'><br>";
  s += "GW: <input name='gw' value='" + cfg.gw.toString() + "'><br>";
  s += "Mask: <input name='mask' value='" + cfg.mask.toString() + "'><br><br>";
  s += "<button type='submit'>Save & Reboot</button>";
  s += "</form>";
  s += "<p><a href='/'>Back</a></p></body></html>";
  server.send(200, "text/html", s);
}

static void handleDmx() {
  String s = htmlHead("DMX/Motion");
  s += "<h2>DMX / Motion</h2>";
  s += "<form method='POST' action='/savedmx'>";
  s += "Universe: <input name='u' type='number' min='1' max='63999' value='" + String(cfg.universe) + "'><br><br>";
  s += "Start Addr: <input name='a' type='number' min='1' max='508' value='" + String(cfg.startAddr) + "'>";
  s += " <small>(uses 4 slots)</small><br><br>";

  s += "sACN Mode: <select name='m'>";
  s += String("<option value='0'") + (cfg.sacnMode==SACN_UNICAST ? " selected" : "") + ">Unicast</option>";
  s += String("<option value='1'") + (cfg.sacnMode==SACN_MULTICAST ? " selected" : "") + ">Multicast</option>";
  s += "</select><br><br>";

  s += "<h3>DC Motor</h3>";
  s += "Deadband: <input name='db' type='number' min='0' max='20000' value='" + String(cfg.dcDeadband) + "'><br><br>";
  s += "Max PWM: <input name='mx' type='number' min='1' max='255' value='" + String(cfg.dcMaxPwm) + "'><br><br>";
  s += "Stop mode: <select name='sm'>";
  s += String("<option value='0'") + (cfg.dcStopMode==DC_COAST ? " selected" : "") + ">Coast</option>";
  s += String("<option value='1'") + (cfg.dcStopMode==DC_BRAKE ? " selected" : "") + ">Brake (treated as Coast)</option>";
  s += "</select><br><br>";

  s += "<h3>Stepper</h3>";
  s += "Steps/Rev: <input name='spr' type='number' min='200' max='20000' value='" + String(cfg.stepsPerRev) + "'><br><br>";
  s += "Max deg/sec: <input name='mdps' type='number' step='0.1' min='1' max='720' value='" + String(cfg.maxDegPerSec) + "'><br><br>";

  s += "<h3>Soft Limits</h3>";
  s += "<label><input name='lim' type='checkbox' " + String(cfg.limitsEnabled ? "checked" : "") + "> Enable limits</label><br><br>";
  s += "Min deg: <input name='min' type='number' step='0.1' value='" + String(cfg.minDeg) + "'><br><br>";
  s += "Max deg: <input name='max' type='number' step='0.1' value='" + String(cfg.maxDeg) + "'><br><br>";

  s += "<h3>DMX Loss</h3>";
  s += "Timeout (ms): <input name='to' type='number' min='100' max='60000' value='" + String(cfg.lossTimeoutMs) + "'><br><br>";
  s += "On loss: <select name='lm'>";
  s += String("<option value='0'") + (cfg.lossMode==LOSS_FORCE_OFF ? " selected" : "") + ">Force OFF</option>";
  s += String("<option value='1'") + (cfg.lossMode==LOSS_FORCE_ON  ? " selected" : "") + ">Force ON (treated as OFF)</option>";
  s += String("<option value='2'") + (cfg.lossMode==LOSS_HOLD_LAST ? " selected" : "") + ">Hold Last</option>";
  s += "</select><br><br>";

  s += "<button type='submit'>Save</button>";
  s += "</form>";
  s += "<p><a href='/'>Back</a></p></body></html>";
  server.send(200, "text/html", s);
}

static void handleSaveWifi() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }
  cfg.ssid = server.arg("ssid");
  cfg.pass = server.arg("pass");
  cfg.useStatic = server.hasArg("st");

  IPAddress ip,gw,mask;
  if (parseIp(server.arg("ip"), ip)) cfg.ip = ip;
  if (parseIp(server.arg("gw"), gw)) cfg.gw = gw;
  if (parseIp(server.arg("mask"), mask)) cfg.mask = mask;

  saveConfig();
  server.send(200, "text/plain", "Saved. Rebooting...");
  delay(400);
  ESP.restart();
}

static void handleSaveDmx() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

  cfg.universe = (uint16_t)server.arg("u").toInt();
  cfg.startAddr = (uint16_t)server.arg("a").toInt();
  cfg.sacnMode = (SacnMode)server.arg("m").toInt();

  cfg.dcDeadband = (int16_t)server.arg("db").toInt();
  cfg.dcMaxPwm = (uint8_t)server.arg("mx").toInt();
  cfg.dcStopMode = (DcStopMode)server.arg("sm").toInt();

  cfg.stepsPerRev = (uint16_t)server.arg("spr").toInt();
  cfg.maxDegPerSec = server.arg("mdps").toFloat();

  cfg.limitsEnabled = server.hasArg("lim");
  cfg.minDeg = server.arg("min").toFloat();
  cfg.maxDeg = server.arg("max").toFloat();

  cfg.lossTimeoutMs = (uint32_t)server.arg("to").toInt();
  cfg.lossMode = (DmxLossMode)server.arg("lm").toInt();

  sanity();
  saveConfig();

  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleHome() {
  // set current step position as 0 degrees
  cfg.homeOffsetSteps = stepCurrent;
  saveConfig();
  server.sendHeader("Location", "/");
  server.send(303);
}

static void setupWeb() {
  server.on("/", handleRoot);
  server.on("/wifi", handleWifi);
  server.on("/dmx", handleDmx);
  server.on("/savewifi", handleSaveWifi);
  server.on("/savedmx", handleSaveDmx);
  server.on("/home", handleHome);

  // Quick test endpoints
  server.on("/test/dc/fwd", [](){
    dcApplySigned(+20000);
    server.sendHeader("Location","/");
    server.send(303);
  });
  server.on("/test/dc/rev", [](){
    dcApplySigned(-20000);
    server.sendHeader("Location","/");
    server.send(303);
  });
  server.on("/test/dc/stop", [](){
    dcStop();
    server.sendHeader("Location","/");
    server.send(303);
  });

  server.on("/test/step/0",   [](){ stepperSetTargetDeg(0);   server.sendHeader("Location","/"); server.send(303); });
  server.on("/test/step/90",  [](){ stepperSetTargetDeg(90);  server.sendHeader("Location","/"); server.send(303); });
  server.on("/test/step/180", [](){ stepperSetTargetDeg(180); server.sendHeader("Location","/"); server.send(303); });
  server.on("/test/step/270", [](){ stepperSetTargetDeg(270); server.sendHeader("Location","/"); server.send(303); });
  server.on("/test/step/360", [](){ stepperSetTargetDeg(360); server.sendHeader("Location","/"); server.send(303); });

  server.begin();
}

// ---------------------------
// Setup / Loop
// ---------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  LittleFS.begin();
  loadConfig();
  sanity();

  // GPIO init
  pinMode(STEP_IN1, OUTPUT);
  pinMode(STEP_IN2, OUTPUT);
  pinMode(STEP_IN3, OUTPUT);
  pinMode(STEP_IN4, OUTPUT);
  stepperWritePhase(0);

  pinMode(DC_DIR_PIN, OUTPUT);
  digitalWrite(DC_DIR_PIN, LOW);

  ledcSetup(DC_PWM_CH, DC_PWM_HZ, DC_PWM_BITS);
  ledcAttachPin(DC_PWM_PIN, DC_PWM_CH);
  dcStop();

  pinMode(HOME_BTN, INPUT_PULLUP);

  // WiFi connect or AP fallback
  bool ok = connectSta(8000);
  if (!ok) startAp();

  setupWeb();
  setupOta();

  // Start sACN only when STA is connected (unicast needs IP)
  if (WiFi.getMode() & WIFI_STA) {
    if (WiFi.status() == WL_CONNECTED) {
      startSacn();
    }
  }

  haveDmx = false;
  lastDmxMs = millis();
}

static void handleHomeButton() {
  bool b = digitalRead(HOME_BTN); // true = not pressed (pullup)
  uint32_t now = millis();

  if (b != lastBtn) {
    lastBtn = b;
    btnStableMs = now;
    return;
  }

  if (!b && (uint32_t)(now - btnStableMs) > 40) {
    // pressed stable
    // latch once per press
    static bool did = false;
    if (!did) {
      did = true;
      cfg.homeOffsetSteps = stepCurrent;
      saveConfig();
    }
  } else if (b) {
    static bool did = false;
    did = false;
  }
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();

  handleHomeButton();

  // Consume sACN packets
  if (e131Started) {
    while (!e131.isEmpty()) {
      e131_packet_t p;
      e131.pull(&p);

      uint16_t u = ntohs(p.universe);
      lastUniverseSeen = u;
      sacnPacketCount++;

      if (u != cfg.universe) continue;
      applyDmxFromPacket(p);
    }
  }

  stepperTick();
  enforceDmxLoss();
  yield();
}

