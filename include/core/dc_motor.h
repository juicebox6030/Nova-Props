#ifndef CORE_DC_MOTOR_H
#define CORE_DC_MOTOR_H

#include <Arduino.h>

void initDcMotor();
void dcStop();
void dcApplySigned(int16_t signedCmd);
int16_t dcLastCommand();

#endif
