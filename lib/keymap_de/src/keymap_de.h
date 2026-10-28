#pragma once
#include <stdint.h>
#include <stddef.h>

enum class KeyboardLayout : uint8_t {
    DE_QWERTZ = 0,
    US_QWERTY = 1
};

// USB HID Modifier Bitmasken (HID Usage Page 0x07)
constexpr uint8_t HID_MOD_NONE   = 0x00;
constexpr uint8_t HID_MOD_LCTRL  = 0x01;
constexpr uint8_t HID_MOD_LSHIFT = 0x02;
constexpr uint8_t HID_MOD_LALT   = 0x04;
constexpr uint8_t HID_MOD_LGUI   = 0x08;
constexpr uint8_t HID_MOD_RCTRL  = 0x10;
constexpr uint8_t HID_MOD_RSHIFT = 0x20;
constexpr uint8_t HID_MOD_RALT   = 0x40; // AltGr auf DE-Tastatur
constexpr uint8_t HID_MOD_RGUI   = 0x80;

struct HidKeyStroke {
    uint8_t modifiers;
    uint8_t keycode;       // HID Usage ID (Page 0x07)
    bool deadKeySpace;     // true = nach Tastenanschlag Space (0x2C) senden (z.B. ^ und ` auf DE)
};

// Liest ein UTF-8-Zeichen aus `utf8` und gibt die Anzahl der konsumierten Bytes zurück (0 bei Ende).
size_t utf8NextCodepoint(const char *utf8, uint32_t &outCodepoint);

// Wandelt einen Unicode-Codepoint in einen USB-HID-Tastenanschlag für das gewählte Layout um.
// Gibt false zurück, falls das Zeichen nicht abgebildet werden kann.
bool mapCodepointToHid(uint32_t codepoint, KeyboardLayout layout, HidKeyStroke &outStroke);
