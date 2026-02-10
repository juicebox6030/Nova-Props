#include "platform/esp32/dmx_sacn.h"

#include <ESPAsyncE131.h>
#include <lwip/def.h>

#include "core/config.h"
#include "core/dc_motor.h"
#include "core/stepper.h"

static ESPAsyncE131 e131(1); // 1 universe buffer
static bool e131Started = false;
static bool haveDmx = false;
static uint32_t lastDmxMs = 0;

static uint32_t sacnPacketCount = 0;
static uint16_t lastUniverseSeenValue = 0;
static uint16_t lastDcRaw16 = 0;
static uint16_t lastStepRaw16 = 0;

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

void startSacn() {
  if (cfg.sacnMode == SACN_MULTICAST) {
    e131.begin(E131_MULTICAST, cfg.universe, 1);
  } else {
    // UNICAST mode: listen on universe, accept unicast to our IP
    e131.begin(E131_UNICAST, cfg.universe, 1);
  }
  e131Started = true;
}

void handleSacnPackets() {
  if (!e131Started) return;

  while (!e131.isEmpty()) {
    e131_packet_t p;
    e131.pull(&p);

    uint16_t u = ntohs(p.universe);
    lastUniverseSeenValue = u;
    sacnPacketCount++;

    if (u != cfg.universe) continue;
    applyDmxFromPacket(p);
  }
}

void enforceDmxLoss() {
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

uint32_t sacnPacketCounter() {
  return sacnPacketCount;
}

uint16_t lastUniverseSeen() {
  return lastUniverseSeenValue;
}

uint16_t lastDcRawValue() {
  return lastDcRaw16;
}

uint16_t lastStepRawValue() {
  return lastStepRaw16;
}

bool dmxActive() {
  return haveDmx;
}
