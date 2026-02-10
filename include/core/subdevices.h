#ifndef CORE_SUBDEVICES_H
#define CORE_SUBDEVICES_H

#include <Arduino.h>

#include "core/config.h"

void initSubdevices();
void tickSubdevices();
void applySacnToSubdevices(uint16_t universe, const uint8_t* dmxSlots, uint16_t slotCount);
void stopSubdevicesOnLoss();

uint16_t subdeviceMinUniverse();
uint16_t subdeviceMaxUniverse();
uint8_t subdeviceSlotWidth(const SubdeviceConfig& sd);

bool addSubdevice(SubdeviceType type, const String& name);
bool deleteSubdevice(uint8_t index);
bool runSubdeviceTest(uint8_t index);
bool homeStepperSubdevice(uint8_t index);

String subdeviceTypeName(SubdeviceType type);

#endif
