#include "core/relay.h"

#include "core/config.h"

static bool relayOn = false;

void initRelay() {
  pinMode(cfg.hardware.relay.pin, OUTPUT);
  setRelay(false);
}

void setRelay(bool on) {
  relayOn = on;
  bool level = cfg.hardware.relay.activeHigh ? on : !on;
  digitalWrite(cfg.hardware.relay.pin, level ? HIGH : LOW);
}

bool relayState() {
  return relayOn;
}
