#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

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

extern AppConfig cfg;
extern const char* CFG_PATH;

bool parseIp(const String& s, IPAddress& out);
void sanity();
bool loadConfig();
bool saveConfig();

#endif
