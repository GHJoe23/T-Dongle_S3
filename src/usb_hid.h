#pragma once
#include <stdint.h>
#include "keymap_de.h"

void usbHidBegin();
void usbHidTypeText(const char *text, KeyboardLayout layout, uint16_t keyDelayMs);
void usbHidCombo(const uint8_t *mods, uint8_t modCount, uint8_t key, KeyboardLayout layout);
void usbHidMouseMove(int8_t dx, int8_t dy, int8_t wheel = 0, int8_t pan = 0);
void usbHidMouseClick(char action);
void usbHidMediaKey(uint16_t usageId);
void usbHidEnterBootloader();
