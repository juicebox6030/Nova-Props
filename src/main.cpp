#include <Arduino.h>

#include "core/features.h"
#include "platform/compat/wifi.h"

#if USE_OTA
#include <ArduinoOTA.h>
#endif

#include <LittleFS.h>

#include "core/config.h"
#include "core/subdevices.h"
#include "core/web_ui.h"
#include "platform/config_storage.h"
#include "platform/dmx_sacn.h"
#include "platform/wifi_ota.h"

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

#if USE_WEB_UI
  setupWeb();
#endif
#if USE_OTA
  setupOta();
#endif

#if USE_SACN
  if (WiFi.getMode() & WIFI_STA) {
    if (WiFi.status() == WL_CONNECTED) {
      startSacn();
    }
  }
#endif
}

void loop() {
#if USE_WEB_UI
  handleWeb();
#endif
#if USE_OTA
  ArduinoOTA.handle();
#endif
#if USE_SACN
  handleSacnPackets();
#endif
  tickSubdevices();
#if USE_SACN
  enforceDmxLoss();
#endif
  yield();
}
