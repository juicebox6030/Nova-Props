#ifndef DC_MOTOR_H
#define DC_MOTOR_H

#include <Arduino.h>

void initDcMotor();
void dcStop();
void dcApplySigned(int16_t signedCmd);
int16_t dcLastCommand();

#endif
