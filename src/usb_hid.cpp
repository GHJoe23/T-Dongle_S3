#include <Arduino.h>
#include "USB.h"
#include "USBHIDKeyboard.h"
#include "USBHIDMouse.h"
#include "usb_hid.h"

static USBHIDKeyboard Keyboard;
static USBHIDMouse Mouse;

void usbHidBegin() {
    // Tarnung: Logitech Unifying-Empfänger (Tastatur+Maus-Kombo)
    USB.VID(0x046D);
    USB.PID(0xC52B);
    USB.manufacturerName("Logitech");
    USB.productName("USB Receiver");

    Keyboard.begin();
    Mouse.begin();
    USB.begin();
}

void usbHidTypeText(const char *text) {
    Keyboard.print(text);
}

void usbHidCombo(const uint8_t *mods, uint8_t modCount, uint8_t key) {
    for (uint8_t i = 0; i < modCount; i++) {
        Keyboard.press(mods[i]);
    }
    if (key) {
        Keyboard.press(key);
    }
    delay(20);
    Keyboard.releaseAll();
}

void usbHidMouseMove(int8_t dx, int8_t dy) {
    Mouse.move(dx, dy);
}
