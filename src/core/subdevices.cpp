#include "core/subdevices.h"

#include "core/features.h"

#if USE_PIXELS
#include <Adafruit_NeoPixel.h>
#endif

#include "core/config.h"

struct StepperState {
  int32_t current = 0;
  int32_t target = 0;
  uint8_t phase = 0;
  uint32_t nextStepDueUs = 0;
  uint8_t lastPosByte = 0;
  bool posInitialized = false;
  bool velocityMode = false;
  int8_t velocityDir = 1;
  uint32_t velocityIntervalUs = 4000;
};

static StepperState stepperStates[MAX_SUBDEVICES];
static bool relayStates[MAX_SUBDEVICES] = {false};
static bool ledStates[MAX_SUBDEVICES] = {false};
static bool dcTestStates[MAX_SUBDEVICES] = {false};
#if USE_PIXELS
static Adafruit_NeoPixel* pixelStrips[MAX_SUBDEVICES] = {nullptr};
static bool pixelTestStates[MAX_SUBDEVICES] = {false};
#endif

static constexpr uint8_t HALFSEQ[8][4] = {
  {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,1,1,0},
  {0,0,1,0}, {0,0,1,1}, {0,0,0,1}, {1,0,0,1},
};

static uint16_t readU16(const uint8_t* dmxSlots, uint16_t addr) {
  uint16_t hi = dmxSlots[addr - 1];
  uint16_t lo = dmxSlots[addr];
  return (uint16_t)((hi << 8) | lo);
}

static void setRelayOutput(uint8_t i, bool on) {
  auto& sd = cfg.subdevices[i];
  relayStates[i] = on;
  bool level = sd.relay.activeHigh ? on : !on;
  digitalWrite(sd.relay.pin, level ? HIGH : LOW);
}

static void setLedOutput(uint8_t i, bool on) {
  auto& sd = cfg.subdevices[i];
  ledStates[i] = on;
  bool level = sd.led.activeHigh ? on : !on;
  digitalWrite(sd.led.pin, level ? HIGH : LOW);
}

uint8_t subdeviceSlotWidth(const SubdeviceConfig& sd) {
  switch (sd.type) {
    case SUBDEVICE_STEPPER: return 2;
    case SUBDEVICE_DC_MOTOR: return 2;
    case SUBDEVICE_RELAY: return 1;
    case SUBDEVICE_LED: return 1;
    case SUBDEVICE_PIXELS: return 3;
    default: return 1;
  }
}

String subdeviceTypeName(SubdeviceType type) {
  switch (type) {
    case SUBDEVICE_STEPPER: return "Stepper";
    case SUBDEVICE_DC_MOTOR: return "DC Motor";
    case SUBDEVICE_RELAY: return "Relay";
    case SUBDEVICE_LED: return "LED";
    case SUBDEVICE_PIXELS: return "Pixel Strip";
    default: return "Unknown";
  }
}

static void initStepperDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.stepper.in1, OUTPUT);
  pinMode(sd.stepper.in2, OUTPUT);
  pinMode(sd.stepper.in3, OUTPUT);
  pinMode(sd.stepper.in4, OUTPUT);
  digitalWrite(sd.stepper.in1, LOW);
  digitalWrite(sd.stepper.in2, LOW);
  digitalWrite(sd.stepper.in3, LOW);
  digitalWrite(sd.stepper.in4, LOW);
  if (sd.stepper.homeSwitchEnabled && sd.stepper.homeSwitchPin != 255) {
    pinMode(sd.stepper.homeSwitchPin, sd.stepper.homeSwitchActiveLow ? INPUT_PULLUP : INPUT);
  }
  stepperStates[i].velocityMode = false;
  stepperStates[i].velocityDir = 1;
  stepperStates[i].velocityIntervalUs = 4000;
}

static void initDcDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.dc.dirPin, OUTPUT);
  digitalWrite(sd.dc.dirPin, LOW);
  ledcSetup(sd.dc.pwmChannel, sd.dc.pwmHz, sd.dc.pwmBits);
  ledcAttachPin(sd.dc.pwmPin, sd.dc.pwmChannel);
  ledcWrite(sd.dc.pwmChannel, 0);
  dcTestStates[i] = false;
}

static void initRelayDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.relay.pin, OUTPUT);
  setRelayOutput(i, false);
}

static void initLedDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.led.pin, OUTPUT);
  setLedOutput(i, false);
}

#if USE_PIXELS
static void initPixelDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  if (pixelStrips[i]) {
    delete pixelStrips[i];
    pixelStrips[i] = nullptr;
  }
  if (sd.pixels.count == 0) return;

  pixelStrips[i] = new Adafruit_NeoPixel(sd.pixels.count, sd.pixels.pin, NEO_GRB + NEO_KHZ800);
  pixelStrips[i]->begin();
  pixelStrips[i]->setBrightness(sd.pixels.brightness);
  pixelStrips[i]->clear();
  pixelStrips[i]->show();
  pixelTestStates[i] = false;
}
#endif

void initSubdevices() {
  for (uint8_t i = 0; i < cfg.subdeviceCount && i < MAX_SUBDEVICES; i++) {
    if (!cfg.subdevices[i].enabled) continue;
    switch (cfg.subdevices[i].type) {
      case SUBDEVICE_STEPPER: initStepperDevice(i); break;
      case SUBDEVICE_DC_MOTOR: initDcDevice(i); break;
      case SUBDEVICE_RELAY: initRelayDevice(i); break;
      case SUBDEVICE_LED: initLedDevice(i); break;
      case SUBDEVICE_PIXELS:
#if USE_PIXELS
        initPixelDevice(i);
#endif
        break;
      default: break;
    }
  }
}

static void tickStepper(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  auto& st = stepperStates[i];

  if (sd.stepper.homeSwitchEnabled && sd.stepper.homeSwitchPin != 255) {
    int level = digitalRead(sd.stepper.homeSwitchPin);
    bool triggered = sd.stepper.homeSwitchActiveLow ? (level == LOW) : (level == HIGH);
    if (triggered) {
      st.current = sd.stepper.homeOffsetSteps;
      st.target = st.current;
      st.velocityMode = false;
      digitalWrite(sd.stepper.in1, LOW);
      digitalWrite(sd.stepper.in2, LOW);
      digitalWrite(sd.stepper.in3, LOW);
      digitalWrite(sd.stepper.in4, LOW);
      return;
    }
  }

  if (!st.velocityMode && st.current == st.target) return;
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - st.nextStepDueUs) < 0) return;

  uint32_t intervalUs = st.velocityMode ? st.velocityIntervalUs : 0;
  if (!st.velocityMode) {
    float stepsPerDeg = (float)sd.stepper.stepsPerRev / 360.0f;
    float maxStepsPerSec = sd.stepper.maxDegPerSec * stepsPerDeg;
    if (maxStepsPerSec < 1.0f) maxStepsPerSec = 1.0f;
    intervalUs = (uint32_t)(1000000.0f / maxStepsPerSec);
  }
  if (intervalUs < 300) intervalUs = 300;

  if (st.velocityMode) {
    if (st.velocityDir >= 0) {
      st.current++;
      st.phase = (st.phase + 1) & 0x07;
    } else {
      st.current--;
      st.phase = (st.phase + 7) & 0x07;
    }
  } else if (st.target > st.current) {
    st.current++;
    st.phase = (st.phase + 1) & 0x07;
  } else {
    st.current--;
    st.phase = (st.phase + 7) & 0x07;
  }

  digitalWrite(sd.stepper.in1, HALFSEQ[st.phase][0] ? HIGH : LOW);
  digitalWrite(sd.stepper.in2, HALFSEQ[st.phase][1] ? HIGH : LOW);
  digitalWrite(sd.stepper.in3, HALFSEQ[st.phase][2] ? HIGH : LOW);
  digitalWrite(sd.stepper.in4, HALFSEQ[st.phase][3] ? HIGH : LOW);

  st.nextStepDueUs = nowUs + intervalUs;
}

void tickSubdevices() {
  for (uint8_t i = 0; i < cfg.subdeviceCount && i < MAX_SUBDEVICES; i++) {
    if (!cfg.subdevices[i].enabled) continue;
    if (cfg.subdevices[i].type == SUBDEVICE_STEPPER) tickStepper(i);
  }
}

void applySacnToSubdevices(uint16_t universe, const uint8_t* dmxSlots, uint16_t slotCount) {
  for (uint8_t i = 0; i < cfg.subdeviceCount && i < MAX_SUBDEVICES; i++) {
    auto& sd = cfg.subdevices[i];
    if (!sd.enabled || sd.map.universe != universe) continue;

    uint8_t width = subdeviceSlotWidth(sd);
    if (sd.map.startAddr < 1) continue;
    if ((uint16_t)(sd.map.startAddr + width - 1) > slotCount) continue;

    switch (sd.type) {
      case SUBDEVICE_DC_MOTOR: {
        int16_t signedCmd = (int16_t)((int32_t)readU16(dmxSlots, sd.map.startAddr) - 32768);
        int32_t v = signedCmd;
        if (abs(v) <= 1) v = 0;
        if (v == 0) { ledcWrite(sd.dc.pwmChannel, 0); break; }
        bool fwd = (v > 0);
        uint32_t mag = (uint32_t)abs(v);
        uint32_t out = (mag * sd.dc.maxPwm) / 32768;
        if (out < 1) out = 1;
        if (out > sd.dc.maxPwm) out = sd.dc.maxPwm;
        digitalWrite(sd.dc.dirPin, fwd ? HIGH : LOW);
        ledcWrite(sd.dc.pwmChannel, (uint8_t)out);
        break;
      }
      case SUBDEVICE_STEPPER: {
        uint8_t angleByte = dmxSlots[sd.map.startAddr - 1];
        uint8_t speedByte = dmxSlots[sd.map.startAddr];
        auto& st = stepperStates[i];

        if (speedByte == 0) {
          st.velocityMode = false;

          if (!st.posInitialized) {
            st.lastPosByte = angleByte;
            st.posInitialized = true;
          }

          int16_t delta = (int16_t)angleByte - (int16_t)st.lastPosByte;
          if (delta < 0) delta += 256;
          st.lastPosByte = angleByte;

          if (delta != 0) {
            float stepsPerTurnByte = (float)sd.stepper.stepsPerRev / 255.0f;
            st.target += (int32_t)lroundf((float)delta * stepsPerTurnByte);
          }
        } else {
          st.velocityMode = true;
          uint8_t mag = 0;
          if (speedByte <= 127) {
            // 1..127 => clockwise fast..slow
            st.velocityDir = 1;
            mag = (uint8_t)(128 - speedByte);
          } else {
            // 128..255 => counter-clockwise slow..fast
            st.velocityDir = -1;
            mag = (uint8_t)(speedByte - 127);
          }

          float speedNorm = (float)mag / 127.0f;
          if (speedNorm < 0.05f) speedNorm = 0.05f;
          float stepsPerDeg = (float)sd.stepper.stepsPerRev / 360.0f;
          float maxStepsPerSec = sd.stepper.maxDegPerSec * stepsPerDeg;
          if (maxStepsPerSec < 1.0f) maxStepsPerSec = 1.0f;
          float commandedStepsPerSec = maxStepsPerSec * speedNorm;
          if (commandedStepsPerSec < 1.0f) commandedStepsPerSec = 1.0f;
          st.velocityIntervalUs = (uint32_t)(1000000.0f / commandedStepsPerSec);
          if (st.velocityIntervalUs < 300) st.velocityIntervalUs = 300;
        }
        break;
      }
      case SUBDEVICE_RELAY: {
        bool on = dmxSlots[sd.map.startAddr - 1] >= 128;
        setRelayOutput(i, on);
        break;
      }
      case SUBDEVICE_LED: {
        bool on = dmxSlots[sd.map.startAddr - 1] >= 128;
        setLedOutput(i, on);
        break;
      }
      case SUBDEVICE_PIXELS: {
#if USE_PIXELS
        if (!pixelStrips[i]) break;
        uint8_t r = dmxSlots[sd.map.startAddr - 1];
        uint8_t g = dmxSlots[sd.map.startAddr];
        uint8_t b = dmxSlots[sd.map.startAddr + 1];
        for (uint16_t p = 0; p < sd.pixels.count; p++) {
          pixelStrips[i]->setPixelColor(p, pixelStrips[i]->Color(r, g, b));
        }
        pixelStrips[i]->show();
#endif
        break;
      }
      default:
        break;
    }
  }
}

void stopSubdevicesOnLoss() {
  for (uint8_t i = 0; i < cfg.subdeviceCount && i < MAX_SUBDEVICES; i++) {
    auto& sd = cfg.subdevices[i];
    if (!sd.enabled) continue;
    switch (sd.type) {
      case SUBDEVICE_DC_MOTOR:
        ledcWrite(sd.dc.pwmChannel, 0);
        break;
      case SUBDEVICE_RELAY:
        setRelayOutput(i, false);
        break;
      case SUBDEVICE_LED:
        setLedOutput(i, false);
        break;
      case SUBDEVICE_PIXELS:
#if USE_PIXELS
        if (pixelStrips[i]) {
          pixelStrips[i]->clear();
          pixelStrips[i]->show();
        }
#endif
        break;
      case SUBDEVICE_STEPPER:
      default:
        break;
    }
  }
}

bool runSubdeviceTest(uint8_t index) {
  if (index >= cfg.subdeviceCount) return false;
  auto& sd = cfg.subdevices[index];

  switch (sd.type) {
    case SUBDEVICE_STEPPER: {
      int32_t delta = (int32_t)(sd.stepper.stepsPerRev / 4);
      stepperStates[index].target = stepperStates[index].current + delta;
      return true;
    }
    case SUBDEVICE_DC_MOTOR: {
      dcTestStates[index] = !dcTestStates[index];
      if (!dcTestStates[index]) {
        ledcWrite(sd.dc.pwmChannel, 0);
      } else {
        digitalWrite(sd.dc.dirPin, HIGH);
        ledcWrite(sd.dc.pwmChannel, (uint8_t)(sd.dc.maxPwm / 2));
      }
      return true;
    }
    case SUBDEVICE_RELAY:
      setRelayOutput(index, !relayStates[index]);
      return true;
    case SUBDEVICE_LED:
      setLedOutput(index, !ledStates[index]);
      return true;
    case SUBDEVICE_PIXELS:
#if USE_PIXELS
      if (!pixelStrips[index]) return false;
      pixelTestStates[index] = !pixelTestStates[index];
      for (uint16_t p = 0; p < sd.pixels.count; p++) {
        if (pixelTestStates[index]) {
          pixelStrips[index]->setPixelColor(p, pixelStrips[index]->Color(255, 255, 255));
        } else {
          pixelStrips[index]->setPixelColor(p, pixelStrips[index]->Color(0, 0, 0));
        }
      }
      pixelStrips[index]->show();
      return true;
#else
      return false;
#endif
    default:
      return false;
  }
}

bool homeZeroSubdevice(uint8_t index) {
  if (index >= cfg.subdeviceCount) return false;
  auto& sd = cfg.subdevices[index];
  if (sd.type != SUBDEVICE_STEPPER) return false;
  auto& st = stepperStates[index];
  st.current = sd.stepper.homeOffsetSteps;
  st.target = st.current;
  st.velocityMode = false;
  st.posInitialized = false;
  return true;
}

uint16_t subdeviceMinUniverse() {
  uint16_t minU = 65535;
  bool found = false;
  for (uint8_t i = 0; i < cfg.subdeviceCount && i < MAX_SUBDEVICES; i++) {
    if (!cfg.subdevices[i].enabled) continue;
    if (!found || cfg.subdevices[i].map.universe < minU) minU = cfg.subdevices[i].map.universe;
    found = true;
  }
  return found ? minU : cfg.universe;
}

uint16_t subdeviceMaxUniverse() {
  uint16_t maxU = 1;
  bool found = false;
  for (uint8_t i = 0; i < cfg.subdeviceCount && i < MAX_SUBDEVICES; i++) {
    if (!cfg.subdevices[i].enabled) continue;
    if (!found || cfg.subdevices[i].map.universe > maxU) maxU = cfg.subdevices[i].map.universe;
    found = true;
  }
  return found ? maxU : cfg.universe;
}

bool addSubdevice(SubdeviceType type, const String& name) {
  if (cfg.subdeviceCount >= MAX_SUBDEVICES) return false;
  uint8_t idx = cfg.subdeviceCount++;
  SubdeviceConfig& sd = cfg.subdevices[idx];
  sd = SubdeviceConfig();
  sd.type = type;
  sd.enabled = true;
  sd.map.universe = cfg.universe;
  sd.map.startAddr = 1;

  String n = name.length() ? name : (subdeviceTypeName(type) + String("-") + String(idx + 1));
  n.toCharArray(sd.name, sizeof(sd.name));
  return true;
}

bool deleteSubdevice(uint8_t index) {
  if (index >= cfg.subdeviceCount) return false;
#if USE_PIXELS
  if (pixelStrips[index]) {
    delete pixelStrips[index];
    pixelStrips[index] = nullptr;
  }
#endif
  for (uint8_t i = index; i + 1 < cfg.subdeviceCount; i++) {
    cfg.subdevices[i] = cfg.subdevices[i + 1];
    stepperStates[i] = stepperStates[i + 1];
    relayStates[i] = relayStates[i + 1];
    ledStates[i] = ledStates[i + 1];
    dcTestStates[i] = dcTestStates[i + 1];
#if USE_PIXELS
    pixelStrips[i] = pixelStrips[i + 1];
    pixelStrips[i + 1] = nullptr;
    pixelTestStates[i] = pixelTestStates[i + 1];
#endif
  }
  cfg.subdeviceCount--;
  return true;
}
