#pragma once
#include <stdint.h>
#include "keymap_de.h"
#include "jiggler.h"

constexpr uint8_t MAX_SNIPPETS = 12;
constexpr uint32_t DEFAULT_BLE_PIN = 400400; // Feste Standard-PIN (K400 Plus)

// Mikrofon-Betriebsmodi (XIAO nRF52840 Sense PDM-Mikrofon)
enum class MicMode : uint8_t {
    OFF = 0,          // PDM-Mikrofon komplett ausgeschaltet (0 uA)
    CLAP_CONTROL = 1, // 2x Klatschen/Klopfen = Jiggler AN/AUS, 3x = Stealth AN/AUS
    BOSS_GUARD = 2,   // Raumwächter: Bei Geräusch > Schwellenwert sofort Boss-Action (z.B. Win+L)
    COMBO_BOTH = 3    // Clap-Control + Live-Pegel kombiniert
};

struct DeviceSettings {
    uint32_t blePin = DEFAULT_BLE_PIN;
    KeyboardLayout layout = KeyboardLayout::DE_QWERTZ;
    uint16_t keyDelayMs = 12;
    bool randomDelay = false;
    uint16_t keyDelayMaxMs = 55;
    uint16_t jiggleMinSec = 30;
    uint16_t jiggleMaxSec = 60;
    JiggleMode jiggleMode = JiggleMode::PING_PONG;
    bool jiggleAutoStart = true;
    bool stealthMode = false;
    uint8_t micMode = (uint8_t)MicMode::CLAP_CONTROL;
    uint8_t micThreshold = 45;   // Schwellenwert 5..98 %
    uint8_t micBossAction = 0;   // 0 = Win+L (Sperren), 1 = Win+D (Desktop), 2 = Mute + Win+D
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
