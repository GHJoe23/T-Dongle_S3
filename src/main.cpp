#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#if !defined(ARDUINO_ARCH_NRF52)
#include "esp_random.h"
#endif
#include "jiggler.h"
#include "keymap_de.h"
#include "macro_engine.h"
#include "storage.h"
#include "usb_hid.h"
#include "ble_remote.h"
#include "display_ui.h"
#include "mic_sensor.h"

#if !defined(ARDUINO_ARCH_NRF52)
#define PIN_BUTTON 0
#define STEALTH_PRESS_MS 1000
#define BOOTLOADER_HOLD_MS 5000
static uint32_t btnDownAt = 0;
static bool bootloaderTriggered = false;
#endif

Jiggler jiggler;
DeviceSettings cfg;

static bool sendCustomReply = false;
static uint32_t lastBleStatSec = 0xFFFFFFFFUL;
static uint32_t lastMouseMoveMs = 0;
static int16_t pendingMouseDx = 0;
static int16_t pendingMouseDy = 0;
static int16_t pendingMouseWheel = 0;
static int16_t pendingMousePan = 0;

static void flushPendingMouseMove() {
    if (pendingMouseDx != 0 || pendingMouseDy != 0 || pendingMouseWheel != 0 || pendingMousePan != 0) {
        int8_t stepDx = (int8_t)constrain(pendingMouseDx, -127, 127);
        int8_t stepDy = (int8_t)constrain(pendingMouseDy, -127, 127);
        int8_t stepWheel = (int8_t)constrain(pendingMouseWheel, -127, 127);
        int8_t stepPan = (int8_t)constrain(pendingMousePan, -127, 127);
        usbHidMouseMove(stepDx, stepDy, stepWheel, stepPan);
        pendingMouseDx -= stepDx;
        pendingMouseDy -= stepDy;
        pendingMouseWheel -= stepWheel;
        pendingMousePan -= stepPan;
    }
}

// ---- Klassische Schnell-Makros (M1..M3) ----
static MacroStep m1steps[] = {
    {0, {ActionType::Combo, "", {0x83, 0, 0, 0}, 1, 'L', nullptr, {0, 0, 0, 0}}}, // Win+L (Sperren)
};
static MacroStep m2steps[] = {
    {0, {ActionType::Combo, "", {0x80, 0x81, 0, 0}, 2, 0xB1, nullptr, {0, 0, 0, 0}}}, // Strg+Shift+Esc (Task-Manager)
};
static MacroStep m3steps[] = {
    {0, {ActionType::Combo, "", {0x83, 0, 0, 0}, 1, 'D', nullptr, {0, 0, 0, 0}}}, // Win+D (Desktop)
};
static Macro macros[] = {{m1steps, 1}, {m2steps, 1}, {m3steps, 1}};
MacroEngine engine(macros, 3);

static uint32_t hwRandomSeed() {
#if defined(ARDUINO_ARCH_NRF52)
    uint32_t s = NRF_FICR->DEVICEID[0] ^ NRF_FICR->DEVICEID[1] ^ micros();
#else
    uint32_t s = (uint32_t)esp_random();
#endif
    return s ? s : 0x9E3779B9UL;
}

static void sendStatusJson() {
    static char buf[240];
    uint32_t now = millis();
#if defined(ARDUINO_ARCH_NRF52)
    const char *hwTag = "XIAO";
#else
    const char *hwTag = "S3";
#endif
    snprintf(
        buf, sizeof(buf),
        "STAT {\"hw\":\"%s\",\"j\":%d,\"min\":%u,\"max\":%u,\"mode\":%u,\"auto\":%d,\"lay\":\"%s\",\"delay\":%u,\"rnd\":%d,\"rmax\":%u,\"stealth\":%d,\"cnt\":%lu,\"next\":%lu,\"pin\":%lu,\"mic\":%u,\"mthr\":%u,\"mboss\":%u,\"mlvl\":%u,\"mtrig\":%lu}",
        hwTag,
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
        (unsigned long)cfg.blePin,
        (unsigned)cfg.micMode,
        (unsigned)cfg.micThreshold,
        (unsigned)cfg.micBossAction,
        (unsigned)micGetLevel(),
        (unsigned long)micGetTriggerCount()
    );
    bleSetStatus(buf);
    sendCustomReply = true;
}

static void sendSnippetList() {
    static char line[240];
    static SnippetSlot slot;
    for (uint8_t i = 0; i < MAX_SNIPPETS; i++) {
        if (storageGetSnippet(i, slot)) {
            snprintf(line, sizeof(line), "SNIP %u|%s|%s", (unsigned)i, slot.title, slot.text);
            bleSend(line);
            delay(10);
        }
    }
    bleSend("SNIP END");
    sendCustomReply = true;
}

static void enterBootloaderMode() {
    bleSend("OK BOOTLOADER");
    uiShowBootloaderScreen();
    usbHidEnterBootloader();
}

static void executeBossAction() {
    uint8_t winMod[1] = {0x83}; // GUI / Windows-Taste
    if (cfg.micBossAction == 0) {
        usbHidCombo(winMod, 1, 'L', cfg.layout);
    } else if (cfg.micBossAction == 1) {
        usbHidCombo(winMod, 1, 'D', cfg.layout);
    } else {
        usbHidMediaKey(0x00E2);
        delay(20);
        usbHidCombo(winMod, 1, 'D', cfg.layout);
    }
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
            lastMouseMoveMs = now;
            pendingMouseDx += (int16_t)a.intParams[0];
            pendingMouseDy += (int16_t)a.intParams[1];
            pendingMouseWheel += (int16_t)a.intParams[2];
            pendingMousePan += (int16_t)a.intParams[3];
            sendCustomReply = true;
            break;

        case ActionType::MouseClick:
            lastMouseMoveMs = now;
            flushPendingMouseMove();
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
                static SnippetSlot s;
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

        case ActionType::ConfigMic:
            if (a.key == 'T') {
                cfg.micMode = (cfg.micMode == 0) ? (uint8_t)MicMode::CLAP_CONTROL : (uint8_t)MicMode::OFF;
            } else if (a.key == 'M') {
                cfg.micMode = (a.intParams[0] != 0) ? (uint8_t)MicMode::CLAP_CONTROL : (uint8_t)MicMode::OFF;
            } else if (a.key == 'C') {
                cfg.micMode = (uint8_t)a.intParams[0];
                cfg.micThreshold = (uint8_t)a.intParams[1];
                cfg.micBossAction = (uint8_t)a.intParams[2];
            }
            storageSaveSettings(cfg);
            micConfigure((MicMode)cfg.micMode, cfg.micThreshold);
            sendStatusJson();
            break;

        case ActionType::Error:
            break;
    }
}

static void onBleLine(const char *line, void *) {
    bool isMouseMove = (line[0] == 'M' && line[1] == ' ');
    if (!isMouseMove && strcmp(line, "SYS STATUS") != 0) {
        uiFlashCommand(line);
    }
    sendCustomReply = false;
    bool ok = engine.handleLine(millis(), line);
    if (!sendCustomReply && !isMouseMove) {
        bleSend(ok ? "OK" : "ERR");
    }
}

void setup() {
    uint32_t seed = hwRandomSeed();
    randomSeed(seed);

#if !defined(ARDUINO_ARCH_NRF52)
    pinMode(PIN_BUTTON, INPUT_PULLUP);
#endif

    // 1. USB HID sofort beim Start initialisieren
    usbHidBegin();

    // 2. Bluetooth LE vor Dateisystem starten (auf nRF52840 wichtig für SoftDevice S140 + InternalFS)
    engine.begin(dispatch, nullptr);
    bleBegin(DEFAULT_BLE_PIN, onBleLine, nullptr);

    // 3. Persistente Konfiguration & Snippets laden
    storageBegin(cfg);
    bleUpdatePasskey(cfg.blePin);

    // 4. Display / Onboard-RGB-LED initialisieren
    uiBegin(cfg.stealthMode);
    uiSetConfigInfo(cfg.layout, cfg.jiggleMode);
    uiSetBleStatus(false, cfg.blePin);

    // 5. Onboard PDM-Mikrofon starten (auf XIAO nRF52840 Sense aktiv, auf ESP32-S3 No-Op)
    micBegin((MicMode)cfg.micMode, cfg.micThreshold);

    // 6. Mouse-Jiggler initialisieren
    uint32_t now = millis();
    jiggler.begin(now, seed);
    jiggler.configure(now, cfg.jiggleMinSec, cfg.jiggleMaxSec, cfg.jiggleMode);
    jiggler.setActive(now, cfg.jiggleAutoStart);

    sendStatusJson();
}

void loop() {
    uint32_t now = millis();

#if defined(ARDUINO_ARCH_NRF52)
    // 1a. XIAO nRF52840 Sense: Internes PDM-Mikrofon (Doppel-Klatsch / Dreifach-Klatsch / Boss-Guard)
    MicEvent mev = micPoll(now);
    if (mev == MicEvent::DOUBLE_CLAP) {
        jiggler.toggle(now);
        uiFlashCommand("CLAP2");
        sendStatusJson();
    } else if (mev == MicEvent::TRIPLE_CLAP) {
        cfg.stealthMode = !cfg.stealthMode;
        storageSaveSettings(cfg);
        uiSetStealth(cfg.stealthMode);
        sendStatusJson();
    } else if (mev == MicEvent::BOSS_TRIGGER) {
        uiFlashCommand("BOSS");
        executeBossAction();
        sendStatusJson();
    }
#else
    // 1b. LilyGO T-Dongle S3: Physischer Gehäuse-Taster an GPIO0
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
                sendStatusJson();
            } else if (held >= 30) {
                jiggler.toggle(now);
                sendStatusJson();
            }
        }
    }
#endif

    // 2. BLE-Kommando-Queue verarbeiten & kumulierte Touchpad-Bewegungen sofort ausführen
    blePoll();
    flushPendingMouseMove();

    // 3. Mouse-Jiggler ausführen
    int8_t dx, dy;
    if (jiggler.poll(now, dx, dy)) {
        usbHidMouseMove(dx, dy);
        uiJiggle(now);
    }

    // 4. Makro-Schritte abarbeiten
    engine.tick(now);

    // 5. BLE-Verbindungsstatus überwachen
    static bool lastBle = false;
    bool ble = bleConnected();
    if (ble != lastBle) {
        lastBle = ble;
        uiSetBleStatus(ble, cfg.blePin);
        if (ble) {
            sendStatusJson();
        }
    }

    // 6. Jede Sekunde Status aktualisieren (außer während aktiver Touchpad-Bewegung)
    if (now / 1000 != lastBleStatSec) {
        lastBleStatSec = now / 1000;
        if (ble && (now - lastMouseMoveMs > 450)) {
            sendStatusJson();
        }
    }

    uiTick(now, jiggler.active(), jiggler.msUntilNext(now) / 1000, jiggler.count());
    delay(1);
}
