#pragma once
#include <stdint.h>
#include "keymap_de.h"
#include "jiggler.h"

constexpr uint8_t MAX_SNIPPETS = 12;
constexpr uint32_t DEFAULT_BLE_PIN = 400400; // Feste Standard-PIN (K400 Plus)

struct DeviceSettings {
    uint32_t blePin = DEFAULT_BLE_PIN;
    KeyboardLayout layout = KeyboardLayout::DE_QWERTZ;
    uint16_t keyDelayMs = 10;
    uint16_t jiggleMinSec = 30;
    uint16_t jiggleMaxSec = 60;
    JiggleMode jiggleMode = JiggleMode::PING_PONG;
    bool jiggleAutoStart = true;
    bool stealthMode = false;
};

struct SnippetSlot {
    bool used = false;
    char title[32] = {0};
    char text[256] = {0};
};

void storageBegin(DeviceSettings &outSettings);
void storageSaveSettings(const DeviceSettings &settings);

bool storageGetSnippet(uint8_t slot, SnippetSlot &outSlot);
bool storageSetSnippet(uint8_t slot, const char *title, const char *text);
bool storageDeleteSnippet(uint8_t slot);
