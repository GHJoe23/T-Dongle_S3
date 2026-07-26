#pragma once
#include <stdint.h>

void usbHidBegin();
void usbHidTypeText(const char *text);
void usbHidCombo(const uint8_t *mods, uint8_t modCount, uint8_t key);
void usbHidMouseMove(int8_t dx, int8_t dy);
