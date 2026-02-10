#include "core/pixels.h"

#include <Adafruit_NeoPixel.h>

#include "core/config.h"

static Adafruit_NeoPixel* pixels = nullptr;

void initPixels() {
  if (pixels) {
    delete pixels;
    pixels = nullptr;
  }

  if (cfg.hardware.pixels.count == 0) return;

  pixels = new Adafruit_NeoPixel(
    cfg.hardware.pixels.count,
    cfg.hardware.pixels.pin,
    NEO_GRB + NEO_KHZ800
  );
  pixels->begin();
  pixels->setBrightness(cfg.hardware.pixels.brightness);
  pixels->show();
}

void setPixelColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
  if (!pixels) return;
  if (index >= cfg.hardware.pixels.count) return;
  pixels->setPixelColor(index, pixels->Color(r, g, b));
}

void setAllPixels(uint8_t r, uint8_t g, uint8_t b) {
  if (!pixels) return;
  for (uint16_t i = 0; i < cfg.hardware.pixels.count; i++) {
    pixels->setPixelColor(i, pixels->Color(r, g, b));
  }
}

void showPixels() {
  if (!pixels) return;
  pixels->show();
}
