#include "platform/esp32/wifi_ota.h"

#include "core/features.h"
#include "platform/compat/wifi.h"

#if USE_OTA
#include <ArduinoOTA.h>
#endif

#include "core/config.h"
#include "platform/platform_services.h"

String deviceName() {
  return platformDeviceName();
}

bool connectSta(uint32_t timeoutMs) {
  if (cfg.ssid.length() == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);

  if (cfg.useStatic) {
    WiFi.config(cfg.ip, cfg.gw, cfg.mask);
  }

  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(50);
    yield();
    if ((uint32_t)(millis() - start) > timeoutMs) break;
  }
  return (WiFi.status() == WL_CONNECTED);
}

void startAp() {
  WiFi.mode(WIFI_AP);
  String ssid = deviceName();
  WiFi.softAP(ssid.c_str());
}

void setupOta() {
#if USE_OTA
  ArduinoOTA.setHostname(deviceName().c_str());
  ArduinoOTA.begin();
#endif
}
