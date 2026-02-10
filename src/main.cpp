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

#if defined(ARDUINO_ARCH_ESP32) && USE_ESP32_DUAL_CORE
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static TaskHandle_t runtimeTaskHandle = nullptr;

static void runtimeLoopTask(void* param) {
  (void)param;
  while (true) {
#if USE_SACN
    handleSacnPackets();
#endif
    tickSubdevices();
#if USE_SACN
    enforceDmxLoss();
#endif
    vTaskDelay(1);
  }
}
#endif

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
  // sACN must run in AP-only mode and STA-connected mode.
  startSacn();
#endif

#if defined(ARDUINO_ARCH_ESP32) && USE_ESP32_DUAL_CORE
  xTaskCreatePinnedToCore(
      runtimeLoopTask,
      "runtime-loop",
      4096,
      nullptr,
      1,
      &runtimeTaskHandle,
      1);
#endif
}

void loop() {
#if USE_WEB_UI
  handleWeb();
#endif
#if USE_OTA
  ArduinoOTA.handle();
#endif
#if defined(ARDUINO_ARCH_ESP32) && USE_ESP32_DUAL_CORE
  vTaskDelay(1);
#else
#if USE_SACN
  handleSacnPackets();
#endif
  tickSubdevices();
#if USE_SACN
  enforceDmxLoss();
#endif
  yield();
#endif
}
