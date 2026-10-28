#include <Arduino.h>
#include <ctype.h>
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

static void sendRawKey(uint8_t modifiers, uint8_t keycode, uint16_t holdMs) {
    KeyReport down = {modifiers, 0, {keycode, 0, 0, 0, 0, 0}};
    Keyboard.sendReport(&down);
    delay(holdMs);
    KeyReport up = {0, 0, {0, 0, 0, 0, 0, 0}};
    Keyboard.sendReport(&up);
    delay(holdMs);
}

void usbHidBegin() {
    // Tarnung: Logitech Unifying Receiver (K400 Plus Tastatur+Maus+Media-Kombo)
    USB.VID(0x046D);
    USB.PID(0xC52B);
    USB.manufacturerName("Logitech");
    USB.productName("USB Receiver");
    USB.serialNumber(""); // Keine verräterische Espressif-MAC als Seriennummer

    Keyboard.begin();
    Mouse.begin();
    ConsumerControl.begin();
    USB.begin();
}

void usbHidTypeText(const char *text, KeyboardLayout layout, uint16_t keyDelayMs) {
    if (!text) return;
    if (keyDelayMs < 2) keyDelayMs = 2;
    uint16_t halfDelay = keyDelayMs / 2;
    if (halfDelay < 2) halfDelay = 2;

    size_t i = 0;
    while (text[i] != 0) {
        uint32_t cp = 0;
        size_t consumed = 1;

        // Escape-Sequenzen für mehrzeilige Textmodule (\n, \t, \\)
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
            sendRawKey(stroke.modifiers, stroke.keycode, halfDelay);
            if (stroke.deadKeySpace) {
                // Auf DE-Tastatur nach Dead-Key (^, `, ´) Space senden
                sendRawKey(HID_MOD_NONE, 0x2C, halfDelay);
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

    sendRawKey(modMask, usageKey, 20);
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
