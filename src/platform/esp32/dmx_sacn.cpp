#include "platform/esp32/dmx_sacn.h"

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

void startSacn() {
  uint16_t minU = subdeviceMinUniverse();
  uint16_t maxU = subdeviceMaxUniverse();
  uint16_t range = (maxU >= minU) ? (maxU - minU + 1) : 1;
  if (range < 1) range = 1;
  if (range > 4) range = 4;

  if (cfg.sacnMode == SACN_MULTICAST) {
    e131.begin(E131_MULTICAST, minU, range);
  } else {
    e131.begin(E131_UNICAST, minU, range);
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

    applySacnToSubdevices(u, &p.property_values[1], 512);
    haveDmx = true;
    lastDmxMs = millis();
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
