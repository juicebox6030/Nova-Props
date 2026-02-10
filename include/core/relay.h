#ifndef CORE_RELAY_H
#define CORE_RELAY_H

#include <Arduino.h>

void initRelay();
void setRelay(bool on);
bool relayState();

#endif
