#include "platform/platform_services.h"

#include "platform/compat/wifi.h"

String platformDeviceName() {
  char buf[32];
#if defined(ESP32)
  uint64_t mac = ESP.getEfuseMac();
  snprintf(buf, sizeof(buf), "Motion-%04X", (uint16_t)(mac & 0xFFFF));
#elif defined(ESP8266)
  snprintf(buf, sizeof(buf), "Motion-%06X", ESP.getChipId());
#else
  snprintf(buf, sizeof(buf), "Motion-Device");
#endif
  return String(buf);
}

bool platformIsStaMode() {
  return (WiFi.getMode() & WIFI_STA) != 0;
}

bool platformIsApMode() {
  return (WiFi.getMode() & WIFI_AP) != 0;
}

String platformStaIp() {
  return WiFi.localIP().toString();
}

String platformApIp() {
  return WiFi.softAPIP().toString();
}
