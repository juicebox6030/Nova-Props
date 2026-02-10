// src/main.cpp
#include <Arduino.h>

#include <WiFi.h>
#include <ArduinoOTA.h>

#include <LittleFS.h>

#include "app_config.h"
#include "dc_motor.h"
#include "dmx_sacn.h"
#include "hardware.h"
#include "stepper.h"
#include "web_ui.h"
#include "wifi_ota.h"

// =====================================================
// ESP32 WROOM-32: sACN -> DC Motor (L298P) + Stepper (ULN2003)
// - sACN E1.31: UNICAST default, optional MULTICAST
// - AP fallback + Web UI + OTA (STA + AP)
// - DC motor: 16-bit center speed (2 slots) => forward/reverse + PWM
// - Stepper: 16-bit position (2 slots) => degrees => steps (ULN2003 half-step)
// - Soft limits (min/max degrees) + manual homing (button + web)
// - Minimal sACN diagnostics + test buttons
//
// IMPORTANT: ArduinoJson v7-safe (no proxy copies).
// =====================================================

// Home button debounce
static bool lastBtn = true;
static uint32_t btnStableMs = 0;

// ---------------------------
// Setup / Loop
// ---------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  LittleFS.begin();
  loadConfig();
  sanity();

  // GPIO init
  initStepper();
  initDcMotor();

  pinMode(HOME_BTN, INPUT_PULLUP);

  // WiFi connect or AP fallback
  bool ok = connectSta(8000);
  if (!ok) startAp();

  setupWeb();
  setupOta();

  // Start sACN only when STA is connected (unicast needs IP)
  if (WiFi.getMode() & WIFI_STA) {
    if (WiFi.status() == WL_CONNECTED) {
      startSacn();
    }
  }
}

static void handleHomeButton() {
  static bool did = false;
  bool b = digitalRead(HOME_BTN); // true = not pressed (pullup)
  uint32_t now = millis();

  if (b != lastBtn) {
    lastBtn = b;
    btnStableMs = now;
    return;
  }

  if (!b && (uint32_t)(now - btnStableMs) > 40) {
    // pressed stable
    // latch once per press
    if (!did) {
      did = true;
      cfg.homeOffsetSteps = stepperCurrentPosition();
      saveConfig();
    }
  } else if (b) {
    did = false;
  }
}

void loop() {
  handleWeb();
  ArduinoOTA.handle();

  handleHomeButton();

  handleSacnPackets();

  stepperTick();
  enforceDmxLoss();
  yield();
}
