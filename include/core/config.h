#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#include <Arduino.h>

enum SacnMode : uint8_t { SACN_UNICAST = 0, SACN_MULTICAST = 1 };
enum DmxLossMode : uint8_t { LOSS_FORCE_OFF = 0, LOSS_FORCE_ON = 1, LOSS_HOLD_LAST = 2 };
enum DcStopMode : uint8_t { DC_COAST = 0, DC_BRAKE = 1 };

enum SubdeviceType : uint8_t {
  SUBDEVICE_STEPPER = 0,
  SUBDEVICE_DC_MOTOR = 1,
  SUBDEVICE_RELAY = 2,
  SUBDEVICE_LED = 3,
  SUBDEVICE_PIXELS = 4,
};

enum StepperDriverType : uint8_t {
  STEPPER_DRIVER_GENERIC = 0,
};

enum StepperSeekMode : uint8_t {
  STEPPER_SEEK_SHORTEST_PATH = 0,
  STEPPER_SEEK_DIRECTIONAL = 1,
};

enum StepperDirection : uint8_t {
  STEPPER_DIR_CW = 0,
  STEPPER_DIR_CCW = 1,
};

enum StepperTieBreakMode : uint8_t {
  STEPPER_TIEBREAK_CW = 0,
  STEPPER_TIEBREAK_CCW = 1,
  STEPPER_TIEBREAK_OPPOSITE_LAST = 2,
};

enum DcDriverType : uint8_t {
  DC_DRIVER_GENERIC = 0,
};

enum PixelDriverType : uint8_t {
  PIXEL_DRIVER_GENERIC = 0,
};

static constexpr uint8_t MAX_SUBDEVICES = 12;

struct SacnMapping {
  uint16_t universe = 1;
  uint16_t startAddr = 1;
};

struct StepperRuntimeConfig {
  StepperDriverType driver = STEPPER_DRIVER_GENERIC;
  uint8_t in1 = 16;
  uint8_t in2 = 17;
  uint8_t in3 = 18;
  uint8_t in4 = 19;
  uint16_t stepsPerRev = 4096;
  float maxDegPerSec = 90.0f;
  bool limitsEnabled = false;
  float minDeg = 0.0f;
  float maxDeg = 360.0f;
  int32_t homeOffsetSteps = 0;
  bool homeSwitchEnabled = false;
  uint8_t homeSwitchPin = 255;
  bool homeSwitchActiveLow = true;
  bool position16Bit = false;
  StepperSeekMode seekMode = STEPPER_SEEK_SHORTEST_PATH;
  StepperDirection seekForwardDirection = STEPPER_DIR_CW;
  StepperDirection seekReturnDirection = STEPPER_DIR_CCW;
  StepperTieBreakMode seekTieBreakMode = STEPPER_TIEBREAK_OPPOSITE_LAST;
  bool seekClockwise = true; // legacy config fallback
};

struct DcMotorRuntimeConfig {
  DcDriverType driver = DC_DRIVER_GENERIC;
  uint8_t dirPin = 25;
  uint8_t pwmPin = 27;
  uint8_t pwmChannel = 0;
  uint32_t pwmHz = 500;
  uint8_t pwmBits = 8;
  int16_t deadband = 900;
  uint8_t maxPwm = 255;
  uint16_t rampBufferMs = 120;
  bool command16Bit = false;
};

struct RelayRuntimeConfig {
  uint8_t pin = 22;
  bool activeHigh = true;
};

struct LedRuntimeConfig {
  uint8_t pin = 21;
  bool activeHigh = true;
};

struct PixelRuntimeConfig {
  PixelDriverType driver = PIXEL_DRIVER_GENERIC;
  uint8_t pin = 26;
  uint16_t count = 30;
  uint8_t brightness = 50;
};

struct SubdeviceConfig {
  bool enabled = true;
  char name[24] = "subdevice";
  SubdeviceType type = SUBDEVICE_STEPPER;
  SacnMapping map;
  StepperRuntimeConfig stepper;
  DcMotorRuntimeConfig dc;
  RelayRuntimeConfig relay;
  LedRuntimeConfig led;
  PixelRuntimeConfig pixels;
};

struct AppConfig {
  String ssid;
  String pass;

  bool useStatic = false;
  IPAddress ip   = IPAddress(192,168,1,60);
  IPAddress gw   = IPAddress(192,168,1,1);
  IPAddress mask = IPAddress(255,255,255,0);

  // retained global defaults
  uint16_t universe = 1;
  uint16_t startAddr = 1;
  SacnMode sacnMode = SACN_UNICAST;
  uint16_t sacnBufferMs = 0;

  DmxLossMode lossMode = LOSS_FORCE_OFF;
  uint32_t lossTimeoutMs = 1000;

  uint8_t homeButtonPin = 23;

  uint8_t subdeviceCount = 0;
  SubdeviceConfig subdevices[MAX_SUBDEVICES];
};

extern AppConfig cfg;

#endif
