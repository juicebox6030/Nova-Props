#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include <Arduino.h>

enum SacnMode : uint8_t { SACN_UNICAST = 0, SACN_MULTICAST = 1 };
enum DmxLossMode : uint8_t { LOSS_FORCE_OFF = 0, LOSS_FORCE_ON = 1, LOSS_HOLD_LAST = 2 };
enum DcStopMode : uint8_t { DC_COAST = 0, DC_BRAKE = 1 };

struct DcMotorConfig {
  uint8_t dirPin = 25;
  uint8_t pwmPin = 27;
  uint8_t pwmChannel = 0;
  uint32_t pwmHz = 500;
  uint8_t pwmBits = 8;
};

struct StepperConfig {
  uint8_t in1 = 16;
  uint8_t in2 = 17;
  uint8_t in3 = 18;
  uint8_t in4 = 19;
};

struct RelayConfig {
  uint8_t pin = 22;
  bool activeHigh = true;
};

struct LedConfig {
  uint8_t pin = 21;
  bool activeHigh = true;
};

struct PixelConfig {
  uint8_t pin = 26;
  uint16_t count = 30;
  uint8_t brightness = 50;
};

struct HardwareConfig {
  DcMotorConfig dcMotor;
  StepperConfig stepper;
  RelayConfig relay;
  LedConfig led;
  PixelConfig pixels;
  uint8_t homeButtonPin = 23;
};

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

  HardwareConfig hardware;
};

extern AppConfig cfg;

#endif
