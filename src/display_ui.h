#pragma once
#include <stdint.h>

void uiBegin();
void uiJiggle(uint32_t now);
void uiTick(uint32_t now, bool active, uint32_t countdownSec, uint32_t jiggleCount);
