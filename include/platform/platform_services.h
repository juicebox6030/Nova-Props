#ifndef PLATFORM_PLATFORM_SERVICES_H
#define PLATFORM_PLATFORM_SERVICES_H

#include <Arduino.h>

String platformDeviceName();
bool platformIsStaMode();
bool platformIsApMode();
String platformStaIp();
String platformApIp();

#endif
