#include "platform/esp32/dmx_sacn.h"

#include "core/features.h"

#if USE_SACN

#include <ESPAsyncE131.h>
#include <lwip/def.h>

#include "core/config.h"
#include "core/subdevices.h"

static ESPAsyncE131 e131(4);
static bool e131Started = false;
static bool haveDmx = false;
static uint32_t lastDmxMs = 0;

static uint32_t sacnPacketCount = 0;
static uint16_t lastUniverseSeenValue = 0;

struct BufferedUniverseFrame {
  bool hasFrame = false;
  uint16_t universe = 0;
  uint32_t lastApplyMs = 0;
  uint8_t slots[512] = {0};
};

static constexpr uint8_t MAX_BUFFERED_UNIVERSES = 4;
static BufferedUniverseFrame bufferedFrames[MAX_BUFFERED_UNIVERSES];

static BufferedUniverseFrame* findBufferedFrame(uint16_t universe, bool create) {
  for (uint8_t i = 0; i < MAX_BUFFERED_UNIVERSES; i++) {
    if (bufferedFrames[i].universe == universe && bufferedFrames[i].universe != 0) return &bufferedFrames[i];
  }
  if (!create) return nullptr;
  for (uint8_t i = 0; i < MAX_BUFFERED_UNIVERSES; i++) {
    if (bufferedFrames[i].universe == 0) {
      bufferedFrames[i].universe = universe;
      bufferedFrames[i].hasFrame = false;
      bufferedFrames[i].lastApplyMs = 0;
      return &bufferedFrames[i];
    }
  }
  return nullptr;
}

void startSacn() {
  e131Started = false;
  uint16_t minU = subdeviceMinUniverse();
  uint16_t maxU = subdeviceMaxUniverse();
  uint16_t range = (maxU >= minU) ? (maxU - minU + 1) : 1;
  if (range < 1) range = 1;
  if (range > 4) range = 4;

  for (uint8_t i = 0; i < MAX_BUFFERED_UNIVERSES; i++) {
    bufferedFrames[i] = BufferedUniverseFrame();
  }

  if (cfg.sacnMode == SACN_MULTICAST) {
    e131.begin(E131_MULTICAST, minU, range);
  } else {
    e131.begin(E131_UNICAST, minU, range);
  }
  e131Started = true;
}

void restartSacn() {
  startSacn();
}

void handleSacnPackets() {
  if (!e131Started) return;

  while (!e131.isEmpty()) {
    e131_packet_t p;
    e131.pull(&p);

    uint16_t u = ntohs(p.universe);
    lastUniverseSeenValue = u;
    sacnPacketCount++;

    if (cfg.sacnBufferMs == 0) {
      applySacnToSubdevices(u, &p.property_values[1], 512);
    } else {
      BufferedUniverseFrame* frame = findBufferedFrame(u, true);
      if (frame) {
        memcpy(frame->slots, &p.property_values[1], sizeof(frame->slots));
        frame->hasFrame = true;
      }
    }

    haveDmx = true;
    lastDmxMs = millis();
  }

  if (cfg.sacnBufferMs == 0) return;

  uint32_t now = millis();
  for (uint8_t i = 0; i < MAX_BUFFERED_UNIVERSES; i++) {
    auto& frame = bufferedFrames[i];
    if (!frame.hasFrame || frame.universe == 0) continue;

    if (frame.lastApplyMs != 0 && (uint32_t)(now - frame.lastApplyMs) < cfg.sacnBufferMs) continue;

    applySacnToSubdevices(frame.universe, frame.slots, 512);
    frame.lastApplyMs = now;
  }
}

void enforceDmxLoss() {
  if (!haveDmx) return;
  uint32_t now = millis();
  if ((uint32_t)(now - lastDmxMs) < cfg.lossTimeoutMs) return;

  haveDmx = false;
  if (cfg.lossMode == LOSS_HOLD_LAST) return;
  stopSubdevicesOnLoss();
}

uint32_t sacnPacketCounter() { return sacnPacketCount; }
uint16_t lastUniverseSeen() { return lastUniverseSeenValue; }
uint16_t lastDcRawValue() { return 0; }
uint16_t lastStepRawValue() { return 0; }
bool dmxActive() { return haveDmx; }

#else

void startSacn() {}
void restartSacn() {}
void handleSacnPackets() {}
void enforceDmxLoss() {}
uint32_t sacnPacketCounter() { return 0; }
uint16_t lastUniverseSeen() { return 0; }
uint16_t lastDcRawValue() { return 0; }
uint16_t lastStepRawValue() { return 0; }
bool dmxActive() { return false; }

#endif
