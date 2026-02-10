#include "core/dc_motor.h"

#include "core/config.h"
static int16_t dcLastCmd = 0;

void initDcMotor() {
  pinMode(cfg.hardware.dcMotor.dirPin, OUTPUT);
  digitalWrite(cfg.hardware.dcMotor.dirPin, LOW);

  ledcSetup(cfg.hardware.dcMotor.pwmChannel, cfg.hardware.dcMotor.pwmHz, cfg.hardware.dcMotor.pwmBits);
  ledcAttachPin(cfg.hardware.dcMotor.pwmPin, cfg.hardware.dcMotor.pwmChannel);
  dcStop();
}

void dcStop() {
  // On this shield, Motor A stop is achieved by PWM=0.
  // (Classic L298 "brake" by shorting outputs isn't exposed via pins here.)
  ledcWrite(cfg.hardware.dcMotor.pwmChannel, 0);
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
  digitalWrite(cfg.hardware.dcMotor.dirPin, fwd ? HIGH : LOW);

  ledcWrite(cfg.hardware.dcMotor.pwmChannel, (uint8_t)out);
}

int16_t dcLastCommand() {
  return dcLastCmd;
}
