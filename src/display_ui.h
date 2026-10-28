#pragma once
#include <stdint.h>
#include "keymap_de.h"
#include "jiggler.h"

void uiBegin(bool stealth = false);
void uiSetStealth(bool stealth);
bool uiIsStealth();
void uiJiggle(uint32_t now);
void uiTick(uint32_t now, bool active, uint32_t countdownSec, uint32_t jiggleCount);
void uiSetBleStatus(bool connected, uint32_t pin);
void uiSetConfigInfo(KeyboardLayout layout, JiggleMode mode);
void uiFlashCommand(const char *cmd);
void uiShowBootloaderScreen();
