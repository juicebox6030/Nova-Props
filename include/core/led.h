#ifndef CORE_LED_H
#define CORE_LED_H

#include <Arduino.h>

void initStatusLed();
void setStatusLed(bool on);
bool statusLedState();

#endif
