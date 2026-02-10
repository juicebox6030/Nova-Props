#ifndef PLATFORM_COMPAT_WIFI_H
#define PLATFORM_COMPAT_WIFI_H

#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#error "Unsupported platform for WiFi compatibility layer"
#endif

#endif
