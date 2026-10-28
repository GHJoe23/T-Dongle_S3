#include <Arduino.h>
#include <stdio.h>
#include "esp_random.h"
#include <FastLED.h>
#include "jiggler.h"
#include "keymap_de.h"
#include "macro_engine.h"
#include "storage.h"
#include "usb_hid.h"
#include "ble_remote.h"
#include "display_ui.h"

#define PIN_BUTTON 0
#define PIN_LED_DI 40
#define PIN_LED_CI 39
#define STEALTH_PRESS_MS 1000
#define BOOTLOADER_HOLD_MS 5000

Jiggler jiggler;
CRGB statusLed;
DeviceSettings cfg;

static uint32_t btnDownAt = 0;
static bool bootloaderTriggered = false;
static bool sendCustomReply = false;
static uint32_t lastBleStatSec = 0xFFFFFFFFUL;

// ---- Klassische Schnell-Makros (M1..M3) ----
static MacroStep m1steps[] = {
    {0, {ActionType::Combo, "", {0x83, 0, 0, 0}, 1, 'L'}}, // Win+L (Sperren)
};
static MacroStep m2steps[] = {
    {0, {ActionType::Combo, "", {0x80, 0x81, 0, 0}, 2, 0xB1}}, // Strg+Shift+Esc (Task-Manager)
};
static MacroStep m3steps[] = {
    {0, {ActionType::Combo, "", {0x83, 0, 0, 0}, 1, 'D'}}, // Win+D (Desktop)
};
static Macro macros[] = {{m1steps, 1}, {m2steps, 1}, {m3steps, 1}};
MacroEngine engine(macros, 3);

static void updateStatusLed() {
    if (cfg.stealthMode) {
        statusLed = CRGB::Black;
    } else if (jiggler.active()) {
        statusLed = CRGB(0, 50, 0); // Grün = Jiggler aktiv
    } else {
        statusLed = CRGB(20, 0, 0); // Schwaches Rot = Jiggler pausiert
    }
    FastLED.show();
}

static void sendStatusJson() {
    char buf[280];
    uint32_t now = millis();
    snprintf(
        buf, sizeof(buf),
        "STAT {\"j\":%d,\"min\":%u,\"max\":%u,\"mode\":%u,\"auto\":%d,\"lay\":\"%s\",\"delay\":%u,\"rnd\":%d,\"rmax\":%u,\"stealth\":%d,\"cnt\":%lu,\"next\":%lu,\"pin\":%lu}",
        jiggler.active() ? 1 : 0,
        (unsigned)cfg.jiggleMinSec,
        (unsigned)cfg.jiggleMaxSec,
        (unsigned)cfg.jiggleMode,
        cfg.jiggleAutoStart ? 1 : 0,
        (cfg.layout == KeyboardLayout::DE_QWERTZ) ? "DE" : "US",
        (unsigned)cfg.keyDelayMs,
        cfg.randomDelay ? 1 : 0,
        (unsigned)cfg.keyDelayMaxMs,
        cfg.stealthMode ? 1 : 0,
        (unsigned long)jiggler.count(),
        (unsigned long)(jiggler.msUntilNext(now) / 1000UL),
        (unsigned long)cfg.blePin
    );
    bleSetStatus(buf);
    sendCustomReply = true;
}

static void sendSnippetList() {
    char line[340];
    SnippetSlot slot;
    for (uint8_t i = 0; i < MAX_SNIPPETS; i++) {
        if (storageGetSnippet(i, slot)) {
            snprintf(line, sizeof(line), "SNIP %u|%s|%s", (unsigned)i, slot.title, slot.text);
            bleSend(line);
            delay(12);
        }
    }
    bleSend("SNIP END");
    sendCustomReply = true;
}

static void enterBootloaderMode() {
    bleSend("OK BOOTLOADER");
    statusLed = CRGB(0, 0, 90);
    FastLED.show();
    uiShowBootloaderScreen();
    usbHidEnterBootloader();
}

static void dispatch(const Action &a, void *) {
    uint32_t now = millis();
    switch (a.type) {
        case ActionType::TypeText:
            usbHidTypeText(a.cstr(), cfg.layout, cfg.keyDelayMs, cfg.randomDelay, cfg.keyDelayMaxMs);
            break;

        case ActionType::Combo:
            usbHidCombo(a.mods, a.modCount, a.key, cfg.layout);
            break;

        case ActionType::ToggleJiggler:
            if (a.intParams[0] < 0) jiggler.toggle(now);
            else jiggler.setActive(now, a.intParams[0] != 0);
            updateStatusLed();
            sendStatusJson();
            break;

        case ActionType::ConfigJiggler:
            cfg.jiggleMinSec = (uint16_t)a.intParams[0];
            cfg.jiggleMaxSec = (uint16_t)a.intParams[1];
            cfg.jiggleMode = (JiggleMode)a.intParams[2];
            cfg.jiggleAutoStart = (a.intParams[3] != 0);
            storageSaveSettings(cfg);
            jiggler.configure(now, cfg.jiggleMinSec, cfg.jiggleMaxSec, cfg.jiggleMode);
            uiSetConfigInfo(cfg.layout, cfg.jiggleMode);
            sendStatusJson();
            break;

        case ActionType::MouseMove:
            usbHidMouseMove((int8_t)a.intParams[0], (int8_t)a.intParams[1],
                            (int8_t)a.intParams[2], (int8_t)a.intParams[3]);
            break;

        case ActionType::MouseClick:
            usbHidMouseClick((char)a.key);
            break;

        case ActionType::MediaKey:
            usbHidMediaKey((uint16_t)a.intParams[0]);
            break;

        case ActionType::SnippetOp: {
            uint8_t slotId = (uint8_t)a.intParams[0];
            if (a.key == 'L') {
                sendSnippetList();
            } else if (a.key == 'R') {
                SnippetSlot s;
                if (storageGetSnippet(slotId, s)) {
                    usbHidTypeText(s.text, cfg.layout, cfg.keyDelayMs, cfg.randomDelay, cfg.keyDelayMaxMs);
                } else {
                    bleSend("ERR slot empty");
                    sendCustomReply = true;
                }
            } else if (a.key == 'S') {
                storageSetSnippet(slotId, a.text, a.cstr());
            } else if (a.key == 'D') {
                storageDeleteSnippet(slotId);
            }
            break;
        }

        case ActionType::SystemCmd:
            if (a.key == 'S') {
                sendStatusJson();
            } else if (a.key == 'H') {
                cfg.stealthMode = (a.intParams[0] != 0);
                storageSaveSettings(cfg);
                uiSetStealth(cfg.stealthMode);
                updateStatusLed();
                sendStatusJson();
            } else if (a.key == 'L') {
                cfg.layout = (a.intParams[0] == 1) ? KeyboardLayout::US_QWERTY : KeyboardLayout::DE_QWERTZ;
                storageSaveSettings(cfg);
                uiSetConfigInfo(cfg.layout, cfg.jiggleMode);
                sendStatusJson();
            } else if (a.key == 'D') {
                cfg.keyDelayMs = (uint16_t)a.intParams[0];
                cfg.randomDelay = (a.intParams[1] != 0);
                cfg.keyDelayMaxMs = (uint16_t)a.intParams[2];
                storageSaveSettings(cfg);
                sendStatusJson();
            } else if (a.key == 'W') {
                delay((uint32_t)a.intParams[0]);
            } else if (a.key == 'P') {
                cfg.blePin = (uint32_t)a.intParams[0];
                storageSaveSettings(cfg);
                bleUpdatePasskey(cfg.blePin);
                uiSetBleStatus(bleConnected(), cfg.blePin);
                sendStatusJson();
            } else if (a.key == 'B') {
                enterBootloaderMode();
            }
            break;

        case ActionType::Error:
            break;
    }
}

static void onBleLine(const char *line, void *) {
    if (!(line[0] == 'M' && line[1] == ' ') && strcmp(line, "SYS STATUS") != 0) {
        uiFlashCommand(line);
    }
    sendCustomReply = false;
    bool ok = engine.handleLine(millis(), line);
    if (!sendCustomReply) {
        bleSend(ok ? "OK" : "ERR");
    }
}

void setup() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    storageBegin(cfg);
    usbHidBegin();

    FastLED.addLeds<APA102, PIN_LED_DI, PIN_LED_CI, BGR>(&statusLed, 1);

    engine.begin(dispatch, nullptr);

    bleBegin(cfg.blePin, onBleLine, nullptr);

    uiBegin(cfg.stealthMode);
    uiSetBleStatus(false, cfg.blePin);
    uiSetConfigInfo(cfg.layout, cfg.jiggleMode);

    uint32_t now = millis();
    jiggler.begin(now, (uint32_t)esp_random());
    jiggler.configure(now, cfg.jiggleMinSec, cfg.jiggleMaxSec, cfg.jiggleMode);
    jiggler.setActive(now, cfg.jiggleAutoStart);
    updateStatusLed();
    sendStatusJson();
}

void loop() {
    uint32_t now = millis();

    // Hardware-Button:
    // < 1s: Jiggler AN/PAUSE
    // 1..4.9s: Stealth-Modus (Display+LED komplett aus/an)
    // >= 5s gehalten: Direkt in den USB-Bootloader / Flash-Modus starten
    bool btn = digitalRead(PIN_BUTTON);
    if (btn == LOW && btnDownAt == 0) {
        btnDownAt = now;
        bootloaderTriggered = false;
    }
    if (btn == LOW && !bootloaderTriggered && (now - btnDownAt >= BOOTLOADER_HOLD_MS)) {
        bootloaderTriggered = true;
        enterBootloaderMode();
    }
    if (btn == HIGH && btnDownAt != 0) {
        uint32_t held = now - btnDownAt;
        btnDownAt = 0;
        if (!bootloaderTriggered) {
            if (held >= STEALTH_PRESS_MS) {
                cfg.stealthMode = !cfg.stealthMode;
                storageSaveSettings(cfg);
                uiSetStealth(cfg.stealthMode);
                updateStatusLed();
                sendStatusJson();
            } else if (held >= 30) {
                jiggler.toggle(now);
                updateStatusLed();
                sendStatusJson();
            }
        }
    }

    blePoll();

    int8_t dx, dy;
    if (jiggler.poll(now, dx, dy)) {
        usbHidMouseMove(dx, dy);
        uiJiggle(now);
    }

    engine.tick(now);

    static bool lastBle = false;
    bool ble = bleConnected();
    if (ble != lastBle) {
        lastBle = ble;
        uiSetBleStatus(ble, cfg.blePin);
        if (ble) {
            sendStatusJson();
        }
    }

    // Jede Sekunde den aktuellen Status (Countdown & Zähler) für BLE aktualisieren
    if (now / 1000 != lastBleStatSec) {
        lastBleStatSec = now / 1000;
        if (ble) {
            sendStatusJson();
        }
    }

    uiTick(now, jiggler.active(), jiggler.msUntilNext(now) / 1000, jiggler.count());
    delay(2);
}
