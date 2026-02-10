#include <Arduino.h>

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#include "core/config.h"
#include "core/subdevices.h"
#include "platform/esp32/config_storage.h"
#include "platform/esp32/dmx_sacn.h"
#include "platform/esp32/web_ui.h"
#include "platform/esp32/wifi_ota.h"

void setup() {
  Serial.begin(115200);
  delay(200);

  LittleFS.begin();
  loadConfig();
  sanity();

  initSubdevices();
  pinMode(cfg.homeButtonPin, INPUT_PULLUP);

  bool ok = connectSta(8000);
  if (!ok) startAp();

  setupWeb();
  setupOta();

  if (WiFi.getMode() & WIFI_STA) {
    if (WiFi.status() == WL_CONNECTED) {
      startSacn();
    }
  }
}

void loop() {
  handleWeb();
  ArduinoOTA.handle();
  handleSacnPackets();
  tickSubdevices();
  enforceDmxLoss();
  yield();
}
