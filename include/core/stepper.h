#ifndef CORE_STEPPER_H
#define CORE_STEPPER_H

#include <Arduino.h>

void initStepper();
void stepperSetTargetDeg(float deg);
void stepperTick();
int32_t stepperCurrentPosition();
float stepsToDeg(int32_t steps);

#endif
