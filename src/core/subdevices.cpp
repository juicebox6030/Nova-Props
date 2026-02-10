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
  uint32_t stepIntervalUs = 1000;
  bool velocityMode = false;
  int8_t velocityDir = 1;
  float velocityDegPerSec = 0.0f;
};

struct DcOutputState {
  bool currentForward = true;
  uint16_t currentDuty = 0;
  bool targetForward = true;
  uint16_t targetDuty = 0;
  int32_t filteredSignedDuty = 0;
  uint32_t lastRampMs = 0;
  bool initialized = false;
};

struct PixelCommand {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
};

static StepperState stepperStates[MAX_SUBDEVICES];
static DcOutputState dcOutputStates[MAX_SUBDEVICES];
static PixelCommand pixelCommands[MAX_SUBDEVICES];
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

static uint32_t computeStepperIntervalUs(uint16_t stepsPerRev, float degPerSec);


static bool readStepperHomeSwitch(const SubdeviceConfig& sd) {
  if (!sd.stepper.homeSwitchEnabled || sd.stepper.homeSwitchPin == 255) return false;
  bool level = digitalRead(sd.stepper.homeSwitchPin) != 0;
  return sd.stepper.homeSwitchActiveLow ? !level : level;
}

static void setStepperCoilsLow(const SubdeviceConfig& sd) {
  digitalWrite(sd.stepper.in1, LOW);
  digitalWrite(sd.stepper.in2, LOW);
  digitalWrite(sd.stepper.in3, LOW);
  digitalWrite(sd.stepper.in4, LOW);
}

static void homeStepperState(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  auto& st = stepperStates[i];
  st.current = sd.stepper.homeOffsetSteps;
  st.target = sd.stepper.homeOffsetSteps;
  st.velocityMode = false;
  st.velocityDegPerSec = 0.0f;
  st.stepIntervalUs = computeStepperIntervalUs(sd.stepper.stepsPerRev, sd.stepper.maxDegPerSec);
  setStepperCoilsLow(sd);
}

static void holdStepperStateOnLoss(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  auto& st = stepperStates[i];
  st.velocityMode = false;
  st.velocityDegPerSec = 0.0f;
  st.target = st.current;
  st.stepIntervalUs = computeStepperIntervalUs(sd.stepper.stepsPerRev, sd.stepper.maxDegPerSec);
  setStepperCoilsLow(sd);
}

static uint32_t computeStepperIntervalUs(uint16_t stepsPerRev, float degPerSec) {
  float stepsPerDeg = (float)stepsPerRev / 360.0f;
  float stepsPerSec = degPerSec * stepsPerDeg;
  if (stepsPerSec < 1.0f) stepsPerSec = 1.0f;
  uint32_t intervalUs = (uint32_t)(1000000.0f / stepsPerSec);
  if (intervalUs < 100) intervalUs = 100;
  return intervalUs;
}

static uint16_t readU16(const uint8_t* dmxSlots, uint16_t addr) {
  uint16_t hi = dmxSlots[addr - 1];
  uint16_t lo = dmxSlots[addr];
  return (uint16_t)((hi << 8) | lo);
}

static int32_t floorDiv(int32_t v, int32_t d) {
  int32_t q = v / d;
  int32_t r = v % d;
  if (r != 0 && ((r > 0) != (d > 0))) q--;
  return q;
}

static int32_t mapPositionToSteps(uint16_t rawPosition, uint16_t rawMax, uint16_t stepsPerRev) {
  if (stepsPerRev <= 1 || rawMax == 0) return 0;
  return (int32_t)(((uint32_t)rawPosition * (uint32_t)(stepsPerRev - 1)) / rawMax);
}

static int32_t computeSeekTargetSteps(const SubdeviceConfig& sd, const StepperState& st, int32_t targetWithinRev) {
  // DMX absolute position should take the shortest path within one revolution.
  // (This avoids always seeking CW/CCW and doing a full wrap when crossing 0.)
  int32_t stepsPerRev = sd.stepper.stepsPerRev;
  if (stepsPerRev <= 0) return st.current;

  int32_t currentRelative = st.current - sd.stepper.homeOffsetSteps;
  int32_t currentWithinRev = currentRelative % stepsPerRev;
  if (currentWithinRev < 0) currentWithinRev += stepsPerRev;

  int32_t delta = targetWithinRev - currentWithinRev;
  int32_t half = stepsPerRev / 2;
  if (delta > half) delta -= stepsPerRev;
  if (delta < -half) delta += stepsPerRev;

  return st.current + delta;
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

static void setDcOutput(uint8_t i, bool forward, uint16_t duty) {
  auto& sd = cfg.subdevices[i];
  auto& state = dcOutputStates[i];
  if (state.currentForward == forward && state.currentDuty == duty) return;
  state.currentForward = forward;
  state.currentDuty = duty;
  state.filteredSignedDuty = forward ? (int32_t)duty : -(int32_t)duty;
  digitalWrite(sd.dc.dirPin, forward ? HIGH : LOW);
  ledcWrite(sd.dc.pwmChannel, (uint8_t)duty);
}

static void setDcTarget(uint8_t i, bool forward, uint16_t duty) {
  auto& state = dcOutputStates[i];
  state.targetForward = forward;
  state.targetDuty = duty;
}

static void tickDc(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  auto& state = dcOutputStates[i];

  uint32_t nowMs = millis();
  int32_t currentSignedDuty = state.currentForward ? (int32_t)state.currentDuty : -(int32_t)state.currentDuty;
  int32_t targetSignedDuty = state.targetForward ? (int32_t)state.targetDuty : -(int32_t)state.targetDuty;

  if (!state.initialized) {
    state.filteredSignedDuty = currentSignedDuty;
    state.lastRampMs = nowMs;
    state.initialized = true;
  }

  if (sd.dc.rampBufferMs == 0) {
    state.filteredSignedDuty = targetSignedDuty;
    setDcOutput(i, state.targetForward, state.targetDuty);
    state.lastRampMs = nowMs;
    return;
  }

  uint32_t elapsedMs = nowMs - state.lastRampMs;
  if (elapsedMs == 0) return;

  int32_t delta = targetSignedDuty - state.filteredSignedDuty;
  if (delta != 0) {
    int32_t step = (int32_t)(((int64_t)delta * (int64_t)elapsedMs) / (int64_t)sd.dc.rampBufferMs);
    if (step == 0) step = (delta > 0) ? 1 : -1;
    state.filteredSignedDuty += step;

    if ((delta > 0 && state.filteredSignedDuty > targetSignedDuty) ||
        (delta < 0 && state.filteredSignedDuty < targetSignedDuty)) {
      state.filteredSignedDuty = targetSignedDuty;
    }
  }

  int32_t filtered = state.filteredSignedDuty;
  bool nextForward = (filtered >= 0);
  uint16_t nextDuty = (uint16_t)abs(filtered);
  if (nextDuty > sd.dc.maxPwm) nextDuty = sd.dc.maxPwm;

  setDcOutput(i, nextForward, nextDuty);
  state.lastRampMs = nowMs;
}

static void applyStepperAbsoluteCommand(uint8_t i, int32_t targetWithinRev) {
  auto& sd = cfg.subdevices[i];
  auto& st = stepperStates[i];

  st.velocityMode = false;
  int32_t target = computeSeekTargetSteps(sd, st, targetWithinRev);
  if (sd.stepper.limitsEnabled) {
    float stepsPerDeg = (float)sd.stepper.stepsPerRev / 360.0f;
    int32_t minTarget = (int32_t)lroundf(sd.stepper.minDeg * stepsPerDeg) + sd.stepper.homeOffsetSteps;
    int32_t maxTarget = (int32_t)lroundf(sd.stepper.maxDeg * stepsPerDeg) + sd.stepper.homeOffsetSteps;
    if (target < minTarget) target = minTarget;
    if (target > maxTarget) target = maxTarget;
  }
  st.target = target;
  st.velocityDegPerSec = 0.0f;
  st.stepIntervalUs = computeStepperIntervalUs(sd.stepper.stepsPerRev, sd.stepper.maxDegPerSec);
}

static void applyStepperVelocityCommand(uint8_t i, uint8_t speedRaw) {
  auto& sd = cfg.subdevices[i];
  auto& st = stepperStates[i];

  st.velocityMode = true;

  // Velocity mapping:
  //   0      = rotation disabled (handled by caller)
  //   1-128  = CW slow -> fast
  //   129-255= CCW fast -> slow
  float t = 0.0f;
  if (speedRaw <= 128) {
    st.velocityDir = 1;
    t = ((float)speedRaw - 1.0f) / 127.0f; // 1..128 => 0..1
  } else {
    st.velocityDir = -1;
    t = (255.0f - (float)speedRaw) / 126.0f; // 129..255 => 1..0
  }

  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  const float minDegPerSec = 1.0f;
  st.velocityDegPerSec = minDegPerSec + (sd.stepper.maxDegPerSec - minDegPerSec) * t;
  st.stepIntervalUs = computeStepperIntervalUs(sd.stepper.stepsPerRev, st.velocityDegPerSec);
  st.target = st.current;
}
uint8_t subdeviceSlotWidth(const SubdeviceConfig& sd) {
  switch (sd.type) {
    case SUBDEVICE_STEPPER: return sd.stepper.position16Bit ? 3 : 2;
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

String stepperDriverTypeName(StepperDriverType type) {
  switch (type) {
    case STEPPER_DRIVER_GENERIC: return "Generic";
    default: return "Unknown";
  }
}

String dcDriverTypeName(DcDriverType type) {
  switch (type) {
    case DC_DRIVER_GENERIC: return "Generic";
    default: return "Unknown";
  }
}

String pixelDriverTypeName(PixelDriverType type) {
  switch (type) {
    case PIXEL_DRIVER_GENERIC: return "Generic";
    default: return "Unknown";
  }
}

static void initStepperDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.stepper.in1, OUTPUT);
  pinMode(sd.stepper.in2, OUTPUT);
  pinMode(sd.stepper.in3, OUTPUT);
  pinMode(sd.stepper.in4, OUTPUT);
  setStepperCoilsLow(sd);
  stepperStates[i] = StepperState();
  stepperStates[i].current = sd.stepper.homeOffsetSteps;
  stepperStates[i].target = sd.stepper.homeOffsetSteps;
  stepperStates[i].stepIntervalUs = computeStepperIntervalUs(sd.stepper.stepsPerRev, sd.stepper.maxDegPerSec);
  if (sd.stepper.homeSwitchEnabled && sd.stepper.homeSwitchPin != 255) {
    pinMode(sd.stepper.homeSwitchPin, sd.stepper.homeSwitchActiveLow ? INPUT_PULLUP : INPUT);
  }
}

static void initDcDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.dc.dirPin, OUTPUT);
  digitalWrite(sd.dc.dirPin, LOW);
  ledcSetup(sd.dc.pwmChannel, sd.dc.pwmHz, sd.dc.pwmBits);
  ledcAttachPin(sd.dc.pwmPin, sd.dc.pwmChannel);
  ledcWrite(sd.dc.pwmChannel, 0);
  dcOutputStates[i] = DcOutputState();
  dcOutputStates[i].initialized = false;
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
  pixelCommands[i] = PixelCommand();
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

  if (readStepperHomeSwitch(sd)) {
    homeStepperState(i);
    return;
  }

  if (!st.velocityMode && st.current == st.target) return;
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - st.nextStepDueUs) < 0) return;

  uint32_t intervalUs = st.stepIntervalUs;

  if (st.velocityMode) {
    if (st.velocityDir >= 0) {
      st.current++;
      st.phase = (st.phase + 1) & 0x07;
    } else {
      st.current--;
      st.phase = (st.phase + 7) & 0x07;
    }
    st.target = st.current;
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
    if (cfg.subdevices[i].type == SUBDEVICE_DC_MOTOR) tickDc(i);
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
        uint16_t raw = readU16(dmxSlots, sd.map.startAddr);
        int16_t signedCmd = (int16_t)((int32_t)raw - 32768);
        int32_t v = signedCmd;
        if (abs(v) <= sd.dc.deadband || raw == 0) v = 0;
        if (v == 0) { setDcTarget(i, true, 0); break; }
        bool fwd = (v > 0);
        uint32_t mag = (uint32_t)abs(v);
        uint32_t out = (mag * sd.dc.maxPwm) / 32768;
        if (out < 1) out = 1;
        if (out > sd.dc.maxPwm) out = sd.dc.maxPwm;
        setDcTarget(i, fwd, (uint16_t)out);
        break;
      }
      case SUBDEVICE_STEPPER: {
        uint8_t speedRaw = 0;
        int32_t targetWithinRev = 0;

        if (sd.stepper.position16Bit) {
          uint16_t positionRaw16 = readU16(dmxSlots, sd.map.startAddr);
          speedRaw = dmxSlots[sd.map.startAddr + 1];
          targetWithinRev = mapPositionToSteps(positionRaw16, 65535, sd.stepper.stepsPerRev);
        } else {
          uint8_t positionRaw8 = dmxSlots[sd.map.startAddr - 1];
          speedRaw = dmxSlots[sd.map.startAddr];
          targetWithinRev = mapPositionToSteps(positionRaw8, 255, sd.stepper.stepsPerRev);
        }

        if (speedRaw == 0) {
          applyStepperAbsoluteCommand(i, targetWithinRev);
          break;
        }

        applyStepperVelocityCommand(i, speedRaw);
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
        if (pixelCommands[i].r == r && pixelCommands[i].g == g && pixelCommands[i].b == b) break;
        pixelCommands[i].r = r;
        pixelCommands[i].g = g;
        pixelCommands[i].b = b;
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
        setDcTarget(i, true, 0);
        setDcOutput(i, true, 0);
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
        holdStepperStateOnLoss(i);
        break;
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
      stepperStates[index].velocityMode = false;
      stepperStates[index].target = stepperStates[index].current + delta;
      return true;
    }
    case SUBDEVICE_DC_MOTOR: {
      dcTestStates[index] = !dcTestStates[index];
      if (!dcTestStates[index]) {
        setDcTarget(index, true, 0);
        setDcOutput(index, true, 0);
      } else {
        setDcTarget(index, true, (uint16_t)(sd.dc.maxPwm / 2));
        setDcOutput(index, true, (uint16_t)(sd.dc.maxPwm / 2));
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

bool homeStepperSubdevice(uint8_t index) {
  if (index >= cfg.subdeviceCount) return false;
  auto& sd = cfg.subdevices[index];
  if (sd.type != SUBDEVICE_STEPPER) return false;
  homeStepperState(index);
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
    dcOutputStates[i] = dcOutputStates[i + 1];
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
