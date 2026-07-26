#include <Arduino.h>
#include "esp_random.h"
#include <FastLED.h>
#include "jiggler.h"
#include "usb_hid.h"
#include "display_ui.h"

#define PIN_BUTTON 0
#define PIN_LED_DI 40
#define PIN_LED_CI 39
#define LONG_PRESS_MS 800

Jiggler jiggler;
CRGB statusLed;

static uint32_t btnDownAt = 0;
static bool longFired = false;

static void setStatusLed(bool active) {
    statusLed = active ? CRGB(0, 60, 0) : CRGB::Black;
    FastLED.show();
}

void setup() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    usbHidBegin();

    FastLED.addLeds<APA102, PIN_LED_DI, PIN_LED_CI, BGR>(&statusLed, 1);

    uiBegin();
    jiggler.begin(millis(), (uint32_t)esp_random());
    setStatusLed(jiggler.active());
}

void loop() {
    uint32_t now = millis();

    // Kurzer Druck: Jiggler AN/PAUSE. Langer Druck: Tastatur-Demo (tippt Text).
    bool btn = digitalRead(PIN_BUTTON);
    if (btn == LOW && btnDownAt == 0) {
        btnDownAt = now;
        longFired = false;
    }
    if (btn == LOW && !longFired && now - btnDownAt > LONG_PRESS_MS) {
        longFired = true;
        usbHidTypeText("dongle-demo");
    }
    if (btn == HIGH && btnDownAt != 0) {
        if (!longFired) {
            jiggler.toggle(now);
            setStatusLed(jiggler.active());
        }
        btnDownAt = 0;
    }

    int8_t dx, dy;
    if (jiggler.poll(now, dx, dy)) {
        usbHidMouseMove(dx, dy);
        uiJiggle(now);
    }

    uiTick(now, jiggler.active(), jiggler.msUntilNext(now) / 1000, jiggler.count());
    delay(2);
}
