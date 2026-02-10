#ifndef PLATFORM_ESP32_CONFIG_STORAGE_H
#define PLATFORM_ESP32_CONFIG_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

#include "core/config.h"

extern const char* CFG_PATH;

bool parseIp(const String& s, IPAddress& out);
void sanity();
bool loadConfig();
bool saveConfig();

#endif
