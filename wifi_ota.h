#ifndef WIFI_OTA_H
#define WIFI_OTA_H

#include <Arduino.h>

String deviceName();
bool connectSta(uint32_t timeoutMs);
void startAp();
void setupOta();

#endif
