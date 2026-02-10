#include "core/led.h"

#include "core/config.h"

static bool ledOn = false;

void initStatusLed() {
  pinMode(cfg.hardware.led.pin, OUTPUT);
  setStatusLed(false);
}

void setStatusLed(bool on) {
  ledOn = on;
  bool level = cfg.hardware.led.activeHigh ? on : !on;
  digitalWrite(cfg.hardware.led.pin, level ? HIGH : LOW);
}

bool statusLedState() {
  return ledOn;
}
