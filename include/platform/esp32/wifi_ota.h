#ifndef PLATFORM_ESP32_WIFI_OTA_H
#define PLATFORM_ESP32_WIFI_OTA_H

#include <Arduino.h>

String deviceName();
bool connectSta(uint32_t timeoutMs);
void startAp();
void setupOta();

#endif
