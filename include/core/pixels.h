#ifndef CORE_PIXELS_H
#define CORE_PIXELS_H

#include <Arduino.h>

void initPixels();
void setPixelColor(uint16_t index, uint8_t r, uint8_t g, uint8_t b);
void setAllPixels(uint8_t r, uint8_t g, uint8_t b);
void showPixels();

#endif
