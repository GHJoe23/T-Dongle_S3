#include <Arduino.h>
#include "esp_random.h"
#include "USB.h"
#include "USBHIDMouse.h"
#include <FastLED.h>
#include "jiggler.h"

#define PIN_BUTTON 0 // BOOT-Taster, aktiv LOW
#define PIN_LED_DI 40
#define PIN_LED_CI 39

USBHIDMouse Mouse;
Jiggler jiggler;
CRGB statusLed;

static bool lastBtn = HIGH;
static uint32_t btnChangedMs = 0;

static void setStatusLed(bool active) {
    statusLed = active ? CRGB(0, 60, 0) : CRGB::Black;
    FastLED.show();
}

void setup() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    // Tarnung: als handelsübliche USB-Maus ausgeben (vor begin() setzen!)
    USB.VID(0x046D);
    USB.PID(0xC077);
    USB.manufacturerName("Logitech");
    USB.productName("USB Optical Mouse");

    Mouse.begin();
    USB.begin();

    FastLED.addLeds<APA102, PIN_LED_DI, PIN_LED_CI, BGR>(&statusLed, 1);

    jiggler.begin(millis(), (uint32_t)esp_random());
    setStatusLed(jiggler.active());
}

void loop() {
    uint32_t now = millis();

    // Taster entprellen, kurzer Druck toggelt AN/PAUSE
    bool btn = digitalRead(PIN_BUTTON);
    if (btn != lastBtn && now - btnChangedMs > 40) {
        btnChangedMs = now;
        lastBtn = btn;
        if (btn == LOW) {
            jiggler.toggle(now);
            setStatusLed(jiggler.active());
        }
    }

    int8_t dx, dy;
    if (jiggler.poll(now, dx, dy)) {
        Mouse.move(dx, dy);
    }

    delay(2);
}
