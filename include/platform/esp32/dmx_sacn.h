#ifndef PLATFORM_ESP32_DMX_SACN_H
#define PLATFORM_ESP32_DMX_SACN_H

#include <Arduino.h>

void startSacn();
void handleSacnPackets();
void enforceDmxLoss();

uint32_t sacnPacketCounter();
uint16_t lastUniverseSeen();
uint16_t lastDcRawValue();
uint16_t lastStepRawValue();
bool dmxActive();

#endif
