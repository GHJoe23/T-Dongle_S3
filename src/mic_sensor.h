#pragma once
#include <stdint.h>
#include "storage.h"

enum class MicEvent : uint8_t {
    NONE = 0,
    DOUBLE_CLAP = 1, // 2x Klatschen/Klopfen -> Jiggler AN/PAUSE
    TRIPLE_CLAP = 2, // 3x Klatschen/Klopfen -> Stealth-Modus AN/AUS
    BOSS_TRIGGER = 3 // Geräusch über Schwellenwert im Boss-Guard-Modus -> Boss-Action!
};

void micBegin(MicMode mode, uint8_t threshold);
void micConfigure(MicMode mode, uint8_t threshold);
MicEvent micPoll(uint32_t now);

// Aktueller Spitzenpegel (0..100 %) für Anzeige in der WebBluetooth-App
uint8_t micGetLevel();
bool micIsRunning();
uint32_t micGetTriggerCount();
