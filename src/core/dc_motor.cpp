#include "core/dc_motor.h"

#include "core/config.h"
#include "platform/esp32/hardware.h"

static int16_t dcLastCmd = 0;

void initDcMotor() {
  pinMode(DC_DIR_PIN, OUTPUT);
  digitalWrite(DC_DIR_PIN, LOW);

  ledcSetup(DC_PWM_CH, DC_PWM_HZ, DC_PWM_BITS);
  ledcAttachPin(DC_PWM_PIN, DC_PWM_CH);
  dcStop();
}

void dcStop() {
  // On this shield, Motor A stop is achieved by PWM=0.
  // (Classic L298 "brake" by shorting outputs isn't exposed via pins here.)
  ledcWrite(DC_PWM_CH, 0);
}

void dcApplySigned(int16_t signedCmd) {
  int32_t v = signedCmd;

  if (abs(v) <= cfg.dcDeadband) v = 0;
  dcLastCmd = (int16_t)v;

  if (v == 0) {
    dcStop();
    return;
  }

  bool fwd = (v > 0);
  uint32_t mag = (uint32_t)abs(v);

  uint32_t out = (mag * cfg.dcMaxPwm) / 32768;
  if (out < 1) out = 1;
  if (out > cfg.dcMaxPwm) out = cfg.dcMaxPwm;

  // Direction pin (EA): HIGH=forward, LOW=reverse (per board table)
  digitalWrite(DC_DIR_PIN, fwd ? HIGH : LOW);

  ledcWrite(DC_PWM_CH, (uint8_t)out);
}

int16_t dcLastCommand() {
  return dcLastCmd;
}
