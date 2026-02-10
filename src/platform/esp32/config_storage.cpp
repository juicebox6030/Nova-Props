#include "platform/esp32/config_storage.h"

const char* CFG_PATH = "/config.json";

bool parseIp(const String& s, IPAddress& out) {
  int a,b,c,d;
  if (sscanf(s.c_str(), "%d.%d.%d.%d", &a,&b,&c,&d) != 4) return false;
  if (a<0||a>255||b<0||b>255||c<0||c>255||d<0||d>255) return false;
  out = IPAddress((uint8_t)a,(uint8_t)b,(uint8_t)c,(uint8_t)d);
  return true;
}

void sanity() {
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
  if (cfg.hardware.dcMotor.pwmBits < 1) cfg.hardware.dcMotor.pwmBits = 1;
  if (cfg.hardware.dcMotor.pwmBits > 16) cfg.hardware.dcMotor.pwmBits = 16;
  if (cfg.hardware.dcMotor.pwmHz < 1) cfg.hardware.dcMotor.pwmHz = 1;
  if (cfg.hardware.dcMotor.pwmChannel > 15) cfg.hardware.dcMotor.pwmChannel = 15;
  if (cfg.hardware.pixels.count > 1024) cfg.hardware.pixels.count = 1024;
}

bool loadConfig() {
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

  cfg.hardware.dcMotor.dirPin = doc["hardware"]["dcMotor"]["dirPin"] | cfg.hardware.dcMotor.dirPin;
  cfg.hardware.dcMotor.pwmPin = doc["hardware"]["dcMotor"]["pwmPin"] | cfg.hardware.dcMotor.pwmPin;
  cfg.hardware.dcMotor.pwmChannel = doc["hardware"]["dcMotor"]["pwmChannel"] | cfg.hardware.dcMotor.pwmChannel;
  cfg.hardware.dcMotor.pwmHz = doc["hardware"]["dcMotor"]["pwmHz"] | cfg.hardware.dcMotor.pwmHz;
  cfg.hardware.dcMotor.pwmBits = doc["hardware"]["dcMotor"]["pwmBits"] | cfg.hardware.dcMotor.pwmBits;

  cfg.hardware.stepper.in1 = doc["hardware"]["stepper"]["in1"] | cfg.hardware.stepper.in1;
  cfg.hardware.stepper.in2 = doc["hardware"]["stepper"]["in2"] | cfg.hardware.stepper.in2;
  cfg.hardware.stepper.in3 = doc["hardware"]["stepper"]["in3"] | cfg.hardware.stepper.in3;
  cfg.hardware.stepper.in4 = doc["hardware"]["stepper"]["in4"] | cfg.hardware.stepper.in4;

  cfg.hardware.relay.pin = doc["hardware"]["relay"]["pin"] | cfg.hardware.relay.pin;
  cfg.hardware.relay.activeHigh = doc["hardware"]["relay"]["activeHigh"] | cfg.hardware.relay.activeHigh;

  cfg.hardware.led.pin = doc["hardware"]["led"]["pin"] | cfg.hardware.led.pin;
  cfg.hardware.led.activeHigh = doc["hardware"]["led"]["activeHigh"] | cfg.hardware.led.activeHigh;

  cfg.hardware.pixels.pin = doc["hardware"]["pixels"]["pin"] | cfg.hardware.pixels.pin;
  cfg.hardware.pixels.count = doc["hardware"]["pixels"]["count"] | cfg.hardware.pixels.count;
  cfg.hardware.pixels.brightness = doc["hardware"]["pixels"]["brightness"] | cfg.hardware.pixels.brightness;

  cfg.hardware.homeButtonPin = doc["hardware"]["homeButtonPin"] | cfg.hardware.homeButtonPin;

  sanity();
  return true;
}

bool saveConfig() {
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

  doc["hardware"]["dcMotor"]["dirPin"] = cfg.hardware.dcMotor.dirPin;
  doc["hardware"]["dcMotor"]["pwmPin"] = cfg.hardware.dcMotor.pwmPin;
  doc["hardware"]["dcMotor"]["pwmChannel"] = cfg.hardware.dcMotor.pwmChannel;
  doc["hardware"]["dcMotor"]["pwmHz"] = cfg.hardware.dcMotor.pwmHz;
  doc["hardware"]["dcMotor"]["pwmBits"] = cfg.hardware.dcMotor.pwmBits;

  doc["hardware"]["stepper"]["in1"] = cfg.hardware.stepper.in1;
  doc["hardware"]["stepper"]["in2"] = cfg.hardware.stepper.in2;
  doc["hardware"]["stepper"]["in3"] = cfg.hardware.stepper.in3;
  doc["hardware"]["stepper"]["in4"] = cfg.hardware.stepper.in4;

  doc["hardware"]["relay"]["pin"] = cfg.hardware.relay.pin;
  doc["hardware"]["relay"]["activeHigh"] = cfg.hardware.relay.activeHigh;

  doc["hardware"]["led"]["pin"] = cfg.hardware.led.pin;
  doc["hardware"]["led"]["activeHigh"] = cfg.hardware.led.activeHigh;

  doc["hardware"]["pixels"]["pin"] = cfg.hardware.pixels.pin;
  doc["hardware"]["pixels"]["count"] = cfg.hardware.pixels.count;
  doc["hardware"]["pixels"]["brightness"] = cfg.hardware.pixels.brightness;

  doc["hardware"]["homeButtonPin"] = cfg.hardware.homeButtonPin;

  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  bool ok = (serializeJson(doc, f) > 0);
  f.close();
  return ok;
}
