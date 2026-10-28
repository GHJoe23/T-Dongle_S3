#include <Arduino.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "USBHIDConsumerControl.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_system.h"
#include "esp32-hal-tinyusb.h"
#include "usb_hid.h"

static USBHIDKeyboard Keyboard;
static USBHIDMouse Mouse;
static USBHIDConsumerControl ConsumerControl;

static void sendRawKey(uint8_t modifiers, uint8_t keycode, uint16_t holdMs, uint16_t gapMs) {
    if (modifiers != 0 && keycode != 0) {
        KeyReport modOnly = {modifiers, 0, {0, 0, 0, 0, 0, 0}};
        Keyboard.sendReport(&modOnly);
        delay(4);
    }
    KeyReport down = {modifiers, 0, {keycode, 0, 0, 0, 0, 0}};
    Keyboard.sendReport(&down);
    delay(holdMs);
    KeyReport up = {0, 0, {0, 0, 0, 0, 0, 0}};
    Keyboard.sendReport(&up);
    delay(gapMs);
}

static uint16_t pickDelayMs(uint16_t minMs, bool randomDelay, uint16_t maxMs) {
    if (!randomDelay || maxMs <= minMs) {
        return minMs;
    }
    uint32_t span = (uint32_t)(maxMs - minMs) + 1U;
    return (uint16_t)(minMs + (esp_random() % span));
}

// Prüft, ob an text[i] ein Inline-Tag wie {W:500}, {PAUSE:500}, {TAB}, {ENTER}, {ESC}, {BS}, {DEL} steht.
// Gibt die Anzahl der konsumierten Zeichen zurück (0, wenn kein bekanntes Tag).
static size_t tryHandleInlineTag(const char *s, uint16_t holdMs, uint16_t gapMs) {
    if (s[0] != '{') return 0;
    const char *close = strchr(s + 1, '}');
    if (!close) return 0;
    size_t tagLen = (size_t)(close - (s + 1));
    if (tagLen == 0 || tagLen > 16) return 0;

    // {W:500} / {WAIT:500} / {PAUSE:500}
    const char *numStart = nullptr;
    if (strncmp(s + 1, "W:", 2) == 0) numStart = s + 3;
    else if (strncmp(s + 1, "WAIT:", 5) == 0) numStart = s + 6;
    else if (strncmp(s + 1, "PAUSE:", 6) == 0) numStart = s + 7;

    if (numStart && numStart < close) {
        long ms = strtol(numStart, nullptr, 10);
        if (ms > 0) {
            if (ms > 30000) ms = 30000;
            delay((uint32_t)ms);
        }
        return tagLen + 2;
    }

    if (tagLen == 3 && strncmp(s + 1, "TAB", 3) == 0) {
        sendRawKey(HID_MOD_NONE, 0x2B, holdMs, gapMs);
        return tagLen + 2;
    }
    if (tagLen == 5 && strncmp(s + 1, "ENTER", 5) == 0) {
        sendRawKey(HID_MOD_NONE, 0x28, holdMs, gapMs);
        return tagLen + 2;
    }
    if (tagLen == 3 && strncmp(s + 1, "ESC", 3) == 0) {
        sendRawKey(HID_MOD_NONE, 0x29, holdMs, gapMs);
        return tagLen + 2;
    }
    if ((tagLen == 2 && strncmp(s + 1, "BS", 2) == 0) ||
        (tagLen == 9 && strncmp(s + 1, "BACKSPACE", 9) == 0)) {
        sendRawKey(HID_MOD_NONE, 0x2A, holdMs, gapMs);
        return tagLen + 2;
    }
    if ((tagLen == 3 && strncmp(s + 1, "DEL", 3) == 0) ||
        (tagLen == 4 && strncmp(s + 1, "ENTF", 4) == 0)) {
        sendRawKey(HID_MOD_NONE, 0x4C, holdMs, gapMs);
        return tagLen + 2;
    }

    return 0;
}

void usbHidBegin() {
    // Tarnung: Logitech Unifying Receiver (K400 Plus Tastatur+Maus+Media-Kombo)
    USB.VID(0x046D);
    USB.PID(0xC52B);
    USB.manufacturerName("Logitech");
    USB.productName("USB Receiver");
    USB.serialNumber("400400C52B");

    Keyboard.begin();
    Mouse.begin();
    ConsumerControl.begin();
    USB.begin();
}

void usbHidTypeText(
    const char *text,
    KeyboardLayout layout,
    uint16_t minDelayMs,
    bool randomDelay,
    uint16_t maxDelayMs
) {
    if (!text) return;
    if (minDelayMs < 4) minDelayMs = 4;
    if (maxDelayMs < minDelayMs) maxDelayMs = minDelayMs;

    size_t i = 0;
    while (text[i] != 0) {
        uint16_t totalDelay = pickDelayMs(minDelayMs, randomDelay, maxDelayMs);
        uint16_t holdMs = totalDelay / 3;
        if (holdMs < 3) holdMs = 3;
        if (holdMs > 25) holdMs = 25;
        uint16_t gapMs = (totalDelay > holdMs) ? (totalDelay - holdMs) : 3;

        // 1. Prüfen auf Inline-Pause / Spezialtasten-Tag ({W:500}, {TAB}, {ENTER}, ...)
        if (text[i] == '{') {
            size_t tagConsumed = tryHandleInlineTag(text + i, holdMs, gapMs);
            if (tagConsumed > 0) {
                i += tagConsumed;
                continue;
            }
        }

        uint32_t cp = 0;
        size_t consumed = 1;

        // 2. Escape-Sequenzen für mehrzeilige Textmodule (\n, \t, \\)
        if (text[i] == '\\' && text[i + 1] != 0) {
            char next = text[i + 1];
            if (next == 'n') {
                cp = '\n';
                consumed = 2;
            } else if (next == 't') {
                cp = '\t';
                consumed = 2;
            } else if (next == '\\') {
                cp = '\\';
                consumed = 2;
            } else {
                consumed = utf8NextCodepoint(text + i, cp);
            }
        } else {
            consumed = utf8NextCodepoint(text + i, cp);
        }

        if (consumed == 0) break;
        i += consumed;

        HidKeyStroke stroke{};
        if (mapCodepointToHid(cp, layout, stroke)) {
            sendRawKey(stroke.modifiers, stroke.keycode, holdMs, gapMs);
            if (stroke.deadKeySpace) {
                // Auf DE-Tastatur nach Dead-Key (^, `, ´) Space senden
                sendRawKey(HID_MOD_NONE, 0x2C, holdMs, gapMs);
            }
            // Bei aktiviertem Random-Modus nach Satzzeichen/Wortende kurze natürliche Denkpause
            if (randomDelay && (cp == ' ' || cp == '.' || cp == ',' || cp == '\n')) {
                delay(20 + (esp_random() % 55));
            }
        }
    }
}

void usbHidCombo(const uint8_t *mods, uint8_t modCount, uint8_t key, KeyboardLayout layout) {
    uint8_t modMask = 0;
    for (uint8_t i = 0; i < modCount; i++) {
        uint8_t m = mods[i];
        if (m >= 0x80 && m <= 0x87) {
            modMask |= (uint8_t)(1U << (m - 0x80));
        }
    }

    uint8_t usageKey = 0;
    if (key >= 0x88) {
        // Arduino KEY_* Konstanten (0x88 + HID Usage ID)
        usageKey = (uint8_t)(key - 0x88);
    } else if (key > 0) {
        // ASCII-Zeichen layout-sensitiv auflösen (wichtig für Strg+Z / Strg+Y auf DE-QWERTZ!)
        HidKeyStroke stroke{};
        uint32_t lowerCp = (uint32_t)tolower((unsigned char)key);
        if (mapCodepointToHid(lowerCp, layout, stroke)) {
            usageKey = stroke.keycode;
        }
    }

    sendRawKey(modMask, usageKey, 20, 15);
}

void usbHidMouseMove(int8_t dx, int8_t dy, int8_t wheel, int8_t pan) {
    Mouse.move(dx, dy, wheel, pan);
}

void usbHidMouseClick(char action) {
    switch (action) {
        case 'L':
            Mouse.click(MOUSE_LEFT);
            break;
        case 'R':
            Mouse.click(MOUSE_RIGHT);
            break;
        case 'M':
            Mouse.click(MOUSE_MIDDLE);
            break;
        case 'D':
            Mouse.click(MOUSE_LEFT);
            delay(65);
            Mouse.click(MOUSE_LEFT);
            break;
        case 'd':
            Mouse.press(MOUSE_LEFT);
            break;
        case 'u':
            Mouse.release(MOUSE_LEFT);
            break;
        case 'r':
            Mouse.press(MOUSE_RIGHT);
            break;
        case 's':
            Mouse.release(MOUSE_RIGHT);
            break;
    }
}

void usbHidMediaKey(uint16_t usageId) {
    ConsumerControl.press(usageId);
    delay(25);
    ConsumerControl.release();
}

void usbHidEnterBootloader() {
    delay(100);
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    usb_persist_restart(RESTART_BOOTLOADER);
    esp_restart();
}
