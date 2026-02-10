#include "platform/esp32/wifi_ota.h"

#include <WiFi.h>
#include <ArduinoOTA.h>

#include "core/config.h"

String deviceName() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[32];
  snprintf(buf, sizeof(buf), "ESP32-Motion-%04X", (uint16_t)(mac & 0xFFFF));
  return String(buf);
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
  WiFi.softAP(ssid.c_str()); // open AP for quick setup
}

void setupOta() {
  ArduinoOTA.setHostname(deviceName().c_str());
  ArduinoOTA.begin();
}
