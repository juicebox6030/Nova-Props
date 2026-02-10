#include "platform/esp32/config_storage.h"

#include "core/subdevices.h"

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
  if (cfg.startAddr > 512) cfg.startAddr = 512;
  if (cfg.lossTimeoutMs < 100) cfg.lossTimeoutMs = 100;
  if (cfg.lossTimeoutMs > 60000) cfg.lossTimeoutMs = 60000;
  if (cfg.subdeviceCount > MAX_SUBDEVICES) cfg.subdeviceCount = MAX_SUBDEVICES;

  for (uint8_t i = 0; i < cfg.subdeviceCount; i++) {
    SubdeviceConfig& sd = cfg.subdevices[i];
    if (sd.map.universe < 1) sd.map.universe = 1;
    if (sd.map.startAddr < 1) sd.map.startAddr = 1;
    if (sd.map.startAddr > 512) sd.map.startAddr = 512;
    if (sd.dc.pwmBits < 1) sd.dc.pwmBits = 1;
    if (sd.dc.pwmBits > 16) sd.dc.pwmBits = 16;
    if (sd.dc.pwmChannel > 15) sd.dc.pwmChannel = 15;
    if (sd.dc.pwmHz < 1) sd.dc.pwmHz = 1;
    if (sd.pixels.count > 1024) sd.pixels.count = 1024;
    if (sd.stepper.stepsPerRev < 200) sd.stepper.stepsPerRev = 200;
    if (sd.stepper.stepsPerRev > 20000) sd.stepper.stepsPerRev = 20000;
    if (sd.stepper.maxDegPerSec < 1.0f) sd.stepper.maxDegPerSec = 1.0f;
    if (sd.stepper.maxDegPerSec > 5000.0f) sd.stepper.maxDegPerSec = 5000.0f;
    if (sd.stepper.homeSwitchEnabled && sd.stepper.homeSwitchPin == 255) sd.stepper.homeSwitchEnabled = false;
    if (sd.stepper.minDeg > sd.stepper.maxDeg) {
      float t = sd.stepper.minDeg;
      sd.stepper.minDeg = sd.stepper.maxDeg;
      sd.stepper.maxDeg = t;
    }
  }
}

static void loadSubdevice(JsonObject obj, SubdeviceConfig& sd) {
  sd.enabled = obj["enabled"] | true;
  sd.type = (SubdeviceType)((int)obj["type"] | (int)SUBDEVICE_STEPPER);
  String name = obj["name"].is<const char*>() ? String(obj["name"].as<const char*>()) : String("subdevice");
  name.toCharArray(sd.name, sizeof(sd.name));

  sd.map.universe = obj["map"]["universe"] | 1;
  sd.map.startAddr = obj["map"]["startAddr"] | 1;

  sd.dc.dirPin = obj["dc"]["dirPin"] | sd.dc.dirPin;
  sd.dc.pwmPin = obj["dc"]["pwmPin"] | sd.dc.pwmPin;
  sd.dc.pwmChannel = obj["dc"]["pwmChannel"] | sd.dc.pwmChannel;
  sd.dc.pwmHz = obj["dc"]["pwmHz"] | sd.dc.pwmHz;
  sd.dc.pwmBits = obj["dc"]["pwmBits"] | sd.dc.pwmBits;
  sd.dc.deadband = obj["dc"]["deadband"] | sd.dc.deadband;
  sd.dc.maxPwm = obj["dc"]["maxPwm"] | sd.dc.maxPwm;

  sd.stepper.in1 = obj["stepper"]["in1"] | sd.stepper.in1;
  sd.stepper.in2 = obj["stepper"]["in2"] | sd.stepper.in2;
  sd.stepper.in3 = obj["stepper"]["in3"] | sd.stepper.in3;
  sd.stepper.in4 = obj["stepper"]["in4"] | sd.stepper.in4;
  sd.stepper.stepsPerRev = obj["stepper"]["stepsPerRev"] | sd.stepper.stepsPerRev;
  sd.stepper.maxDegPerSec = obj["stepper"]["maxDegPerSec"] | sd.stepper.maxDegPerSec;
  sd.stepper.limitsEnabled = obj["stepper"]["limitsEnabled"] | sd.stepper.limitsEnabled;
  sd.stepper.minDeg = obj["stepper"]["minDeg"] | sd.stepper.minDeg;
  sd.stepper.maxDeg = obj["stepper"]["maxDeg"] | sd.stepper.maxDeg;
  sd.stepper.homeOffsetSteps = obj["stepper"]["homeOffsetSteps"] | sd.stepper.homeOffsetSteps;
  sd.stepper.homeSwitchEnabled = obj["stepper"]["homeSwitchEnabled"] | sd.stepper.homeSwitchEnabled;
  sd.stepper.homeSwitchPin = obj["stepper"]["homeSwitchPin"] | sd.stepper.homeSwitchPin;
  sd.stepper.homeSwitchActiveLow = obj["stepper"]["homeSwitchActiveLow"] | sd.stepper.homeSwitchActiveLow;

  sd.relay.pin = obj["relay"]["pin"] | sd.relay.pin;
  sd.relay.activeHigh = obj["relay"]["activeHigh"] | sd.relay.activeHigh;

  sd.led.pin = obj["led"]["pin"] | sd.led.pin;
  sd.led.activeHigh = obj["led"]["activeHigh"] | sd.led.activeHigh;

  sd.pixels.pin = obj["pixels"]["pin"] | sd.pixels.pin;
  sd.pixels.count = obj["pixels"]["count"] | sd.pixels.count;
  sd.pixels.brightness = obj["pixels"]["brightness"] | sd.pixels.brightness;
}

static void saveSubdevice(JsonArray arr, const SubdeviceConfig& sd) {
  JsonObject obj = arr.add<JsonObject>();
  obj["enabled"] = sd.enabled;
  obj["type"] = (int)sd.type;
  obj["name"] = sd.name;
  obj["map"]["universe"] = sd.map.universe;
  obj["map"]["startAddr"] = sd.map.startAddr;

  obj["dc"]["dirPin"] = sd.dc.dirPin;
  obj["dc"]["pwmPin"] = sd.dc.pwmPin;
  obj["dc"]["pwmChannel"] = sd.dc.pwmChannel;
  obj["dc"]["pwmHz"] = sd.dc.pwmHz;
  obj["dc"]["pwmBits"] = sd.dc.pwmBits;
  obj["dc"]["deadband"] = sd.dc.deadband;
  obj["dc"]["maxPwm"] = sd.dc.maxPwm;

  obj["stepper"]["in1"] = sd.stepper.in1;
  obj["stepper"]["in2"] = sd.stepper.in2;
  obj["stepper"]["in3"] = sd.stepper.in3;
  obj["stepper"]["in4"] = sd.stepper.in4;
  obj["stepper"]["stepsPerRev"] = sd.stepper.stepsPerRev;
  obj["stepper"]["maxDegPerSec"] = sd.stepper.maxDegPerSec;
  obj["stepper"]["limitsEnabled"] = sd.stepper.limitsEnabled;
  obj["stepper"]["minDeg"] = sd.stepper.minDeg;
  obj["stepper"]["maxDeg"] = sd.stepper.maxDeg;
  obj["stepper"]["homeOffsetSteps"] = sd.stepper.homeOffsetSteps;
  obj["stepper"]["homeSwitchEnabled"] = sd.stepper.homeSwitchEnabled;
  obj["stepper"]["homeSwitchPin"] = sd.stepper.homeSwitchPin;
  obj["stepper"]["homeSwitchActiveLow"] = sd.stepper.homeSwitchActiveLow;

  obj["relay"]["pin"] = sd.relay.pin;
  obj["relay"]["activeHigh"] = sd.relay.activeHigh;

  obj["led"]["pin"] = sd.led.pin;
  obj["led"]["activeHigh"] = sd.led.activeHigh;

  obj["pixels"]["pin"] = sd.pixels.pin;
  obj["pixels"]["count"] = sd.pixels.count;
  obj["pixels"]["brightness"] = sd.pixels.brightness;
}

bool loadConfig() {
  if (!LittleFS.exists(CFG_PATH)) {
    if (cfg.subdeviceCount == 0) {
      addSubdevice(SUBDEVICE_DC_MOTOR, "dc-1");
      cfg.subdevices[0].map.startAddr = 1;
      addSubdevice(SUBDEVICE_STEPPER, "stepper-1");
      cfg.subdevices[1].map.startAddr = 3;
    }
    return false;
  }

  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  cfg.ssid = doc["wifi"]["ssid"].is<const char*>() ? String(doc["wifi"]["ssid"].as<const char*>()) : String("");
  cfg.pass = doc["wifi"]["pass"].is<const char*>() ? String(doc["wifi"]["pass"].as<const char*>()) : String("");
  cfg.useStatic = doc["wifi"]["static"]["enabled"] | false;

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
  cfg.homeButtonPin = doc["hardware"]["homeButtonPin"] | cfg.homeButtonPin;

  cfg.subdeviceCount = 0;
  if (doc["subdevices"].is<JsonArray>()) {
    JsonArray arr = doc["subdevices"].as<JsonArray>();
    for (JsonVariant v : arr) {
      if (cfg.subdeviceCount >= MAX_SUBDEVICES || !v.is<JsonObject>()) break;
      SubdeviceConfig sd;
      loadSubdevice(v.as<JsonObject>(), sd);
      cfg.subdevices[cfg.subdeviceCount++] = sd;
    }
  }

  if (cfg.subdeviceCount == 0) {
    addSubdevice(SUBDEVICE_DC_MOTOR, "dc-1");
    cfg.subdevices[0].map.startAddr = 1;
    addSubdevice(SUBDEVICE_STEPPER, "stepper-1");
    cfg.subdevices[1].map.startAddr = 3;
  }

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
  doc["hardware"]["homeButtonPin"] = cfg.homeButtonPin;

  JsonArray arr = doc["subdevices"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.subdeviceCount; i++) {
    saveSubdevice(arr, cfg.subdevices[i]);
  }

  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  bool ok = (serializeJson(doc, f) > 0);
  f.close();
  return ok;
}
