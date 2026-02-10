#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>

// ---------------------------
// Pin map (ESP32 safe pins)
// ---------------------------
// ULN2003 stepper IN1–IN4
static constexpr uint8_t STEP_IN1 = 16;
static constexpr uint8_t STEP_IN2 = 17;
static constexpr uint8_t STEP_IN3 = 18;
static constexpr uint8_t STEP_IN4 = 19;

// L298P Motor Shield (Motor A) control model (per board table):
//   Direction: EA -> Arduino D3  (we wire to ESP32 GPIO below)
//   PWM:       MA -> Arduino D6  (we wire to ESP32 GPIO below)
// This shield uses ONE direction pin + ONE PWM pin for Motor A.
static constexpr uint8_t DC_DIR_PIN = 25;   // ESP32 GPIO -> Shield D3 (EA)
static constexpr uint8_t DC_PWM_PIN = 27;   // ESP32 GPIO -> Shield D6 (MA)

// Manual Home button (to GND)
static constexpr uint8_t HOME_BTN = 23;

// ---------------------------
// PWM config (ESP32 LEDC)
// ---------------------------
static constexpr uint8_t  DC_PWM_CH   = 0;
static constexpr uint32_t DC_PWM_HZ   = 500;
static constexpr uint8_t  DC_PWM_BITS = 8;       // 0..255

#endif
