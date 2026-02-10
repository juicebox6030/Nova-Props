#include "core/hardware_devices.h"

#include "core/dc_motor.h"
#include "core/led.h"
#include "core/pixels.h"
#include "core/relay.h"
#include "core/stepper.h"

void applyHardwareConfig() {
  initDcMotor();
  initStepper();
  initRelay();
  initStatusLed();
  initPixels();
}
