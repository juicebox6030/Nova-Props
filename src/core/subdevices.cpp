#include "core/subdevices.h"

#include <Adafruit_NeoPixel.h>

#include "core/config.h"

struct StepperState {
  int32_t current = 0;
  int32_t target = 0;
  uint8_t phase = 0;
  uint32_t nextStepDueUs = 0;
};

static StepperState stepperStates[MAX_SUBDEVICES];
static Adafruit_NeoPixel* pixelStrips[MAX_SUBDEVICES] = {nullptr};

static constexpr uint8_t HALFSEQ[8][4] = {
  {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,1,1,0},
  {0,0,1,0}, {0,0,1,1}, {0,0,0,1}, {1,0,0,1},
};

static uint16_t readU16(const uint8_t* dmxSlots, uint16_t addr) {
  uint16_t hi = dmxSlots[addr - 1];
  uint16_t lo = dmxSlots[addr];
  return (uint16_t)((hi << 8) | lo);
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
}

static void initDcDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.dc.dirPin, OUTPUT);
  digitalWrite(sd.dc.dirPin, LOW);
  ledcSetup(sd.dc.pwmChannel, sd.dc.pwmHz, sd.dc.pwmBits);
  ledcAttachPin(sd.dc.pwmPin, sd.dc.pwmChannel);
  ledcWrite(sd.dc.pwmChannel, 0);
}

static void initRelayDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.relay.pin, OUTPUT);
  digitalWrite(sd.relay.pin, sd.relay.activeHigh ? LOW : HIGH);
}

static void initLedDevice(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  pinMode(sd.led.pin, OUTPUT);
  digitalWrite(sd.led.pin, sd.led.activeHigh ? LOW : HIGH);
}

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
}

void initSubdevices() {
  for (uint8_t i = 0; i < cfg.subdeviceCount && i < MAX_SUBDEVICES; i++) {
    if (!cfg.subdevices[i].enabled) continue;
    switch (cfg.subdevices[i].type) {
      case SUBDEVICE_STEPPER: initStepperDevice(i); break;
      case SUBDEVICE_DC_MOTOR: initDcDevice(i); break;
      case SUBDEVICE_RELAY: initRelayDevice(i); break;
      case SUBDEVICE_LED: initLedDevice(i); break;
      case SUBDEVICE_PIXELS: initPixelDevice(i); break;
      default: break;
    }
  }
}

static void tickStepper(uint8_t i) {
  auto& sd = cfg.subdevices[i];
  auto& st = stepperStates[i];
  if (st.current == st.target) return;
  uint32_t nowUs = micros();
  if ((int32_t)(nowUs - st.nextStepDueUs) < 0) return;

  float stepsPerDeg = (float)sd.stepper.stepsPerRev / 360.0f;
  float maxStepsPerSec = sd.stepper.maxDegPerSec * stepsPerDeg;
  if (maxStepsPerSec < 1.0f) maxStepsPerSec = 1.0f;
  uint32_t intervalUs = (uint32_t)(1000000.0f / maxStepsPerSec);
  if (intervalUs < 300) intervalUs = 300;

  if (st.target > st.current) {
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
        if (abs(v) <= sd.dc.deadband) v = 0;
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
        uint16_t raw = readU16(dmxSlots, sd.map.startAddr);
        float deg = ((float)raw / 65535.0f) * 360.0f;
        if (sd.stepper.limitsEnabled) {
          if (deg < sd.stepper.minDeg) deg = sd.stepper.minDeg;
          if (deg > sd.stepper.maxDeg) deg = sd.stepper.maxDeg;
        }
        float stepsPerDeg = (float)sd.stepper.stepsPerRev / 360.0f;
        stepperStates[i].target = (int32_t)lroundf(deg * stepsPerDeg) + sd.stepper.homeOffsetSteps;
        break;
      }
      case SUBDEVICE_RELAY: {
        bool on = dmxSlots[sd.map.startAddr - 1] >= 128;
        bool level = sd.relay.activeHigh ? on : !on;
        digitalWrite(sd.relay.pin, level ? HIGH : LOW);
        break;
      }
      case SUBDEVICE_LED: {
        bool on = dmxSlots[sd.map.startAddr - 1] >= 128;
        bool level = sd.led.activeHigh ? on : !on;
        digitalWrite(sd.led.pin, level ? HIGH : LOW);
        break;
      }
      case SUBDEVICE_PIXELS: {
        if (!pixelStrips[i]) break;
        uint8_t r = dmxSlots[sd.map.startAddr - 1];
        uint8_t g = dmxSlots[sd.map.startAddr];
        uint8_t b = dmxSlots[sd.map.startAddr + 1];
        for (uint16_t p = 0; p < sd.pixels.count; p++) {
          pixelStrips[i]->setPixelColor(p, pixelStrips[i]->Color(r, g, b));
        }
        pixelStrips[i]->show();
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
        digitalWrite(sd.relay.pin, sd.relay.activeHigh ? LOW : HIGH);
        break;
      case SUBDEVICE_LED:
        digitalWrite(sd.led.pin, sd.led.activeHigh ? LOW : HIGH);
        break;
      case SUBDEVICE_PIXELS:
        if (pixelStrips[i]) {
          pixelStrips[i]->clear();
          pixelStrips[i]->show();
        }
        break;
      case SUBDEVICE_STEPPER:
      default:
        break;
    }
  }
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
  if (pixelStrips[index]) {
    delete pixelStrips[index];
    pixelStrips[index] = nullptr;
  }
  for (uint8_t i = index; i + 1 < cfg.subdeviceCount; i++) {
    cfg.subdevices[i] = cfg.subdevices[i + 1];
    stepperStates[i] = stepperStates[i + 1];
    pixelStrips[i] = pixelStrips[i + 1];
    pixelStrips[i + 1] = nullptr;
  }
  cfg.subdeviceCount--;
  return true;
}
