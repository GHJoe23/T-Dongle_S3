# HID-Kombo + BLE-Fernbedienung Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** T-Dongle-S3 als USB-HID-Kombigerät (Tastatur+Maus, Logitech-Unifying-Optik) mit BLE-Fernbedienung vom Smartphone (Text, Kürzel, Makros), bestehender Jiggler bleibt.

**Architecture:** Arduino-ESP32 Core 3.3.9 (pioarduino 55.03.39), Composite-HID aus `USBHIDKeyboard` + `USBHIDMouse` (je eigenes `USBHID`-Member → 2 Interfaces), BLE via NimBLE-Arduino (GATT-UART, Passkey-Bonding mit Display-PIN). Reine Kommando-/Makrologik als host-testbares Modul (`macro_engine`).

**Tech Stack:** PlatformIO (venv, Wrapper `tools/pio-win.bat`), Arduino_GFX, FastLED, NimBLE-Arduino 2.5, Unity (native Tests via zig-Shims in `tools/shims/`).

**Spec:** `docs/superpowers/specs/2026-07-21-t-dongle-s3-hid-combo-ble-design.md`
**Vorgänger-Plan (Toolchain-Kontext):** `docs/superpowers/plans/2026-07-21-t-dongle-s3-mouse-jiggler.md`

## Global Constraints

- USB: VID `0x046D`, PID `0xC52B`, Manufacturer `Logitech`, Product `USB Receiver`; kein COM-Port, kein MSC, kein JTAG
- BLE: NimBLE (kein Bluedroid — kein PSRAM); GATT Nordic-UART: Service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, RX `6E400002-…` (Write, verschlüsselt), TX `6E400003-…` (Notify)
- Sicherheit: Bonding + MITM + Secure Connections, IO-Cap DisplayOnly, Passkey zufällig bei jedem Boot
- Protokoll: `T <text>`, `K <MOD+TASTE>`, `M<n>` (n=1..9), `J`; Antwort `OK`/`ERR …` via TX
- Modifier-Namen: `STRG`=0x80, `UMSCHALT`=0x81, `ALT`=0x82, `GUI`=0x83; Sondertasten: `ENTF`=0xD4, `ESC`=0xB1, `ENTER`=0xB0, `TAB`=0xB3, `F1..F12`=0xC2..0xCD, `LEER`=Leerzeichen
- Tastatur v1: ASCII/US-Layout (`Keyboard.print`)
- Pins unverändert: Taster GPIO0, APA102 DI=40/CI=39, TFT MOSI=3/SCK=5/CS=4/DC=2/RST=1/BL=38
- Builds/Uploads immer über `cmd //c 'tools\pio-win.bat …'` (entfernt MSYSTEM, UTF-8); native Tests mit `tools\shims` im PATH
- Flash-Vorgang nach bestehender Firmware: BOOT halten beim Einstecken → Download-Modus (COM-Port per PowerShell ermitteln) → `run -t upload --upload-port COMx` → ohne BOOT umstecken

---

### Task 1: macro_engine (TDD, native Tests)

**Files:**
- Create: `lib/macro_engine/src/macro_engine.h`
- Create: `lib/macro_engine/src/macro_engine.cpp`
- Create: `test/test_macro_engine/test_macro_engine.cpp`

**Interfaces:**
- Produces (konsumiert in Task 3):

```cpp
enum class ActionType : uint8_t { TypeText, Combo, ToggleJiggler, Error };
struct Action {
    ActionType type;
    char text[64];      // TypeText: Text; Error: Grund
    uint8_t mods[4];    // Combo: Modifier-Codes
    uint8_t modCount;
    uint8_t key;        // Combo: Taste (ASCII oder KEY_*-Code)
};
struct MacroStep { uint32_t delayMs; Action action; };
struct Macro { const MacroStep* steps; uint8_t count; };
class MacroEngine {
public:
    typedef void (*EmitFn)(const Action& a, void* ctx);
    MacroEngine(const Macro* macros, uint8_t macroCount);
    void begin(EmitFn emit, void* ctx);
    bool handleLine(uint32_t now, const char* line); // false = Error emittiert
    bool busy() const;
    void tick(uint32_t now);
};
```

- [ ] **Step 1: Failing Tests schreiben**

`test/test_macro_engine/test_macro_engine.cpp`:

```cpp
#include <unity.h>
#include <string.h>
#include "macro_engine.h"

void setUp(void) {}
void tearDown(void) {}

static Action emitted[16];
static int emittedCount;
static void capture(const Action &a, void *) { emitted[emittedCount++] = a; }

static MacroStep m1steps[] = {
    {0, {ActionType::Combo, "", {0x83, 0, 0, 0}, 1, 'L'}},
    {100, {ActionType::TypeText, "hi", {0, 0, 0, 0}, 0, 0}},
};
static MacroStep m2steps[] = {
    {0, {ActionType::Combo, "", {0x80, 0x82, 0, 0}, 2, 0xD4}},
};
static Macro macros[] = {{m1steps, 2}, {m2steps, 1}};

static MacroEngine *newEngine() {
    static MacroEngine e(macros, 2);
    e.begin(capture, nullptr);
    emittedCount = 0;
    return &e;
}

void test_parse_type_text() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "T hallo"));
    TEST_ASSERT_EQUAL(1, emittedCount);
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::TypeText);
    TEST_ASSERT_EQUAL_STRING("hallo", emitted[0].text);
}

void test_parse_toggle_jiggler() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "J"));
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::ToggleJiggler);
}

void test_parse_combo_modifiers() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "K STRG+ALT+ENTF"));
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::Combo);
    TEST_ASSERT_EQUAL(2, emitted[0].modCount);
    TEST_ASSERT_EQUAL_UINT8(0x80, emitted[0].mods[0]);
    TEST_ASSERT_EQUAL_UINT8(0x82, emitted[0].mods[1]);
    TEST_ASSERT_EQUAL_UINT8(0xD4, emitted[0].key);
}

void test_parse_combo_gui_letter() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "K GUI+L"));
    TEST_ASSERT_EQUAL(1, emitted[0].modCount);
    TEST_ASSERT_EQUAL_UINT8(0x83, emitted[0].mods[0]);
    TEST_ASSERT_EQUAL_UINT8('L', emitted[0].key);
}

void test_macro_executes_steps_with_timing() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "M1"));
    TEST_ASSERT_TRUE(e->busy());
    e->tick(0); // Schritt 1 sofort
    TEST_ASSERT_EQUAL(1, emittedCount);
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::Combo);
    e->tick(50); // Schritt 2 noch nicht fällig (100ms)
    TEST_ASSERT_EQUAL(1, emittedCount);
    e->tick(100);
    TEST_ASSERT_EQUAL(2, emittedCount);
    TEST_ASSERT_TRUE(emitted[1].type == ActionType::TypeText);
    TEST_ASSERT_EQUAL_STRING("hi", emitted[1].text);
    TEST_ASSERT_FALSE(e->busy());
}

void test_macro_busy_rejects_second() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_TRUE(e->handleLine(0, "M1"));
    TEST_ASSERT_FALSE(e->handleLine(10, "M2"));
    TEST_ASSERT_TRUE(emitted[emittedCount - 1].type == ActionType::Error);
}

void test_unknown_macro_and_command() {
    MacroEngine *e = newEngine();
    TEST_ASSERT_FALSE(e->handleLine(0, "M9"));
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::Error);
    emittedCount = 0;
    TEST_ASSERT_FALSE(e->handleLine(0, "X foo"));
    TEST_ASSERT_TRUE(emitted[0].type == ActionType::Error);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_type_text);
    RUN_TEST(test_parse_toggle_jiggler);
    RUN_TEST(test_parse_combo_modifiers);
    RUN_TEST(test_parse_combo_gui_letter);
    RUN_TEST(test_macro_executes_steps_with_timing);
    RUN_TEST(test_macro_busy_rejects_second);
    RUN_TEST(test_unknown_macro_and_command);
    return UNITY_END();
}
```

- [ ] **Step 2: Tests laufen lassen → müssen fehlschlagen**

```bash
cmd //c 'set PATH=%CD%\tools\shims;%PATH% && set MSYSTEM= && .venv\Scripts\pio.exe test -e native'
```

Erwartet: FAIL/ERRORED (`macro_engine.h` nicht gefunden)

- [ ] **Step 3: Implementierung**

`lib/macro_engine/src/macro_engine.h`:

```cpp
#pragma once
#include <stdint.h>

enum class ActionType : uint8_t { TypeText, Combo, ToggleJiggler, Error };

struct Action {
    ActionType type;
    char text[64];
    uint8_t mods[4];
    uint8_t modCount;
    uint8_t key;
};

struct MacroStep {
    uint32_t delayMs;
    Action action;
};

struct Macro {
    const MacroStep *steps;
    uint8_t count;
};

class MacroEngine {
public:
    typedef void (*EmitFn)(const Action &a, void *ctx);

    MacroEngine(const Macro *macros, uint8_t macroCount);
    void begin(EmitFn emit, void *ctx);
    bool handleLine(uint32_t now, const char *line);
    bool busy() const { return _macroIdx >= 0; }
    void tick(uint32_t now);

private:
    const Macro *_macros;
    uint8_t _macroCount;
    EmitFn _emit = nullptr;
    void *_ctx = nullptr;
    int8_t _macroIdx = -1;
    uint8_t _step = 0;
    uint32_t _nextAt = 0;

    bool parseCombo(const char *s, Action &out);
    void emitError(const char *reason);
    void startMacro(uint32_t now, uint8_t idx);
};
```

`lib/macro_engine/src/macro_engine.cpp`:

```cpp
#include "macro_engine.h"
#include <string.h>
#include <ctype.h>

MacroEngine::MacroEngine(const Macro *macros, uint8_t macroCount)
    : _macros(macros), _macroCount(macroCount) {}

void MacroEngine::begin(EmitFn emit, void *ctx) {
    _emit = emit;
    _ctx = ctx;
    _macroIdx = -1;
    _step = 0;
}

void MacroEngine::emitError(const char *reason) {
    Action a{};
    a.type = ActionType::Error;
    strncpy(a.text, reason, sizeof(a.text) - 1);
    if (_emit) _emit(a, _ctx);
}

static bool tokenEq(const char *tok, int len, const char *name) {
    return (int)strlen(name) == len && strncmp(tok, name, len) == 0;
}

bool MacroEngine::parseCombo(const char *s, Action &out) {
    // Format: MOD+MOD+TASTE (letztes Token = Taste)
    memset(&out, 0, sizeof(out));
    out.type = ActionType::Combo;
    char buf[48];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *tokens[8];
    int n = 0;
    char *p = strtok(buf, "+");
    while (p && n < 8) { tokens[n++] = p; p = strtok(nullptr, "+"); }
    if (n == 0) return false;

    for (int i = 0; i < n - 1; i++) {
        const char *t = tokens[i];
        uint8_t code;
        if (strcmp(t, "STRG") == 0) code = 0x80;
        else if (strcmp(t, "UMSCHALT") == 0) code = 0x81;
        else if (strcmp(t, "ALT") == 0) code = 0x82;
        else if (strcmp(t, "GUI") == 0) code = 0x83;
        else return false;
        if (out.modCount >= 4) return false;
        out.mods[out.modCount++] = code;
    }

    const char *k = tokens[n - 1];
    int len = strlen(k);
    if (len == 1) {
        out.key = (uint8_t)toupper((unsigned char)k[0]);
    } else if (tokenEq(k, len, "ENTF")) out.key = 0xD4;
    else if (tokenEq(k, len, "ESC")) out.key = 0xB1;
    else if (tokenEq(k, len, "ENTER")) out.key = 0xB0;
    else if (tokenEq(k, len, "TAB")) out.key = 0xB3;
    else if (tokenEq(k, len, "LEER")) out.key = ' ';
    else if (k[0] == 'F' && len >= 2) {
        int f = atoi(k + 1);
        if (f < 1 || f > 12) return false;
        out.key = 0xC1 + (uint8_t)f; // F1=0xC2 .. F12=0xCD
    } else return false;
    return true;
}

bool MacroEngine::handleLine(uint32_t now, const char *line) {
    if (line[0] == 'J' && line[1] == 0) {
        Action a{};
        a.type = ActionType::ToggleJiggler;
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (line[0] == 'T' && line[1] == ' ') {
        Action a{};
        a.type = ActionType::TypeText;
        strncpy(a.text, line + 2, sizeof(a.text) - 1);
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (line[0] == 'K' && line[1] == ' ') {
        Action a;
        if (!parseCombo(line + 2, a)) {
            emitError("bad combo");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (line[0] == 'M' && line[1] >= '1' && line[1] <= '9' && line[2] == 0) {
        uint8_t idx = (uint8_t)(line[1] - '1');
        if (idx >= _macroCount) {
            emitError("unknown macro");
            return false;
        }
        if (busy()) {
            emitError("busy");
            return false;
        }
        startMacro(now, idx);
        return true;
    }
    emitError("unknown command");
    return false;
}

void MacroEngine::startMacro(uint32_t now, uint8_t idx) {
    _macroIdx = (int8_t)idx;
    _step = 0;
    _nextAt = now + _macros[idx].steps[0].delayMs;
}

void MacroEngine::tick(uint32_t now) {
    if (_macroIdx < 0 || (int32_t)(now - _nextAt) < 0) return;
    const Macro *m = &_macros[_macroIdx];
    if (_emit) _emit(m->steps[_step].action, _ctx);
    _step++;
    if (_step >= m->count) {
        _macroIdx = -1;
    } else {
        _nextAt = now + m->steps[_step].delayMs;
    }
}
```

- [ ] **Step 4: Tests laufen lassen → müssen grün sein**

```bash
cmd //c 'set PATH=%CD%\tools\shims;%PATH% && set MSYSTEM= && .venv\Scripts\pio.exe test -e native'
```

Erwartet: `7 test cases: 7 succeeded`

- [ ] **Step 5: Commit**

```bash
git add lib/macro_engine test/test_macro_engine
git commit -m "feat: macro_engine (Kommando-Parser + Makro-Abarbeitung) mit nativen Tests"
```

---

### Task 2: USB-Composite (Tastatur+Maus) + Integration

**Files:**
- Create: `src/usb_hid.h`
- Create: `src/usb_hid.cpp`
- Modify: `src/main.cpp` (komplett ersetzen)

**Interfaces:**
- Consumes: `Jiggler::{begin,toggle,active,poll,msUntilNext,count}` (besteht), `uiBegin/uiJiggle/uiTick` (besteht)
- Produces (konsumiert in Task 3):

```cpp
void usbHidBegin();
void usbHidTypeText(const char* text);
void usbHidCombo(const uint8_t* mods, uint8_t modCount, uint8_t key);
void usbHidMouseMove(int8_t dx, int8_t dy);
```

- [ ] **Step 1: `src/usb_hid.h` anlegen**

```cpp
#pragma once
#include <stdint.h>

void usbHidBegin();
void usbHidTypeText(const char *text);
void usbHidCombo(const uint8_t *mods, uint8_t modCount, uint8_t key);
void usbHidMouseMove(int8_t dx, int8_t dy);
```

- [ ] **Step 2: `src/usb_hid.cpp` anlegen**

```cpp
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
```

- [ ] **Step 3: `src/main.cpp` ersetzen**

```cpp
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
```

- [ ] **Step 4: Build**

```bash
cmd //c 'tools\pio-win.bat run -e T-Dongle-S3'
```

Erwartet: `SUCCESS`

- [ ] **Step 5: Flashen (Download-Modus)**

1. Benutzer: BOOT halten beim Einstecken, loslassen
2. Port ermitteln:

```bash
powershell.exe -NoProfile -Command 'Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "COM\d" } | Select-Object Name, DeviceID'
```

3. `cmd //c 'tools\pio-win.bat run -e T-Dongle-S3 -t upload --upload-port COMx'`
4. Ohne BOOT umstecken

- [ ] **Step 6: Composite-Tarnung Windows-seitig verifizieren**

```bash
powershell.exe -NoProfile -Command 'Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -like "*VID_046D&PID_C52B*" } | Select-Object Name, DeviceID; Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "COM\d" } | Select-Object Name'
```

Erwartet: `USB-Verbundgerät` und/oder `USB-Eingabegerät` mit `VID_046D&PID_C52B`, Tastatur- und Maus-HID-Einträge, **kein** COM-Port.

- [ ] **Step 7: Tastatur-Funktionstest durch Benutzer**

Texteditor öffnen, Dongle-Taster **lang** (>0,8 s) drücken → `dongle-demo` wird getippt. Kurzer Druck toggelt weiterhin den Jiggler (LED).

- [ ] **Step 8: Commit**

```bash
git add src/usb_hid.h src/usb_hid.cpp src/main.cpp
git commit -m "feat: USB-Composite Tastatur+Maus (Unifying-Tarnung), Tastatur-Demo via Langdruck"
```

---

### Task 3: BLE-Fernbedienung (NimBLE, Passkey) + Display-Status

**Files:**
- Create: `src/ble_remote.h`
- Create: `src/ble_remote.cpp`
- Modify: `src/display_ui.h`, `src/display_ui.cpp`
- Modify: `src/main.cpp`
- Modify: `platformio.ini` (NimBLE-Dependency)
- Modify: `README.md` (BLE-Nutzung)

**Interfaces:**
- Consumes: `MacroEngine`, `Action`, `ActionType` (Task 1); `usbHid*` (Task 2); `Jiggler` (besteht)
- Produces:

```cpp
typedef void (*BleLineCallback)(const char *line, void *ctx);
void bleBegin(uint32_t passkey, BleLineCallback onLine, void *ctx);
void bleSend(const char *msg);
bool bleConnected();
// display_ui neu:
void uiSetBleStatus(bool connected, uint32_t pin);
void uiFlashCommand(const char *cmd);
```

- [ ] **Step 1: NimBLE-Dependency ergänzen**

In `platformio.ini` im `[env:T-Dongle-S3]` unter `lib_deps` hinzufügen:

```ini
    h2zero/NimBLE-Arduino@^2.5.0
```

- [ ] **Step 2: `src/ble_remote.h` anlegen**

```cpp
#pragma once
#include <stdint.h>

typedef void (*BleLineCallback)(const char *line, void *ctx);

void bleBegin(uint32_t passkey, BleLineCallback onLine, void *ctx);
void bleSend(const char *msg);
bool bleConnected();
```

- [ ] **Step 3: `src/ble_remote.cpp` anlegen**

```cpp
#include <Arduino.h>
#include <NimBLEDevice.h>
#include "ble_remote.h"

static const char *SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *CHAR_RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
static const char *CHAR_TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

static NimBLECharacteristic *txChar = nullptr;
static BleLineCallback lineCb = nullptr;
static void *lineCbCtx = nullptr;
static bool connected = false;
static char lineBuf[96];
static uint8_t lineLen = 0;

class RxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &connInfo) override {
        std::string v = c->getValue();
        for (char ch : v) {
            if (ch == '\n' || ch == '\r') {
                if (lineLen > 0 && lineCb) {
                    lineBuf[lineLen] = 0;
                    lineCb(lineBuf, lineCbCtx);
                }
                lineLen = 0;
            } else if (lineLen < sizeof(lineBuf) - 1) {
                lineBuf[lineLen++] = ch;
            }
        }
    }
};

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *s, NimBLEConnInfo &connInfo) override { connected = true; }
    void onDisconnect(NimBLEServer *s, NimBLEConnInfo &connInfo, int reason) override {
        connected = false;
        NimBLEDevice::startAdvertising();
    }
};

void bleBegin(uint32_t passkey, BleLineCallback onLine, void *ctx) {
    lineCb = onLine;
    lineCbCtx = ctx;

    NimBLEDevice::init("USB Receiver");
    NimBLEDevice::setSecurityAuth(true, true, true); // Bonding, MITM, Secure Connections
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey(passkey);

    NimBLEServer *server = NimBLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());
    server->advertiseOnDisconnect(true);

    NimBLEService *svc = server->createService(SERVICE_UUID);
    txChar = svc->createCharacteristic(CHAR_TX_UUID, NIMBLE_PROPERTY::NOTIFY);
    NimBLECharacteristic *rxChar = svc->createCharacteristic(
        CHAR_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC);
    rxChar->setCallbacks(new RxCallbacks());
    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->start();
}

void bleSend(const char *msg) {
    if (txChar && connected) {
        txChar->setValue((const uint8_t *)msg, strlen(msg));
        txChar->notify();
    }
}

bool bleConnected() {
    return connected;
}
```

- [ ] **Step 4: `display_ui` erweitern**

In `src/display_ui.h` hinzufügen:

```cpp
void uiSetBleStatus(bool connected, uint32_t pin);
void uiFlashCommand(const char *cmd);
```

In `src/display_ui.cpp` hinzufügen (State + Rendering in `uiTick`):

```cpp
static bool bleState = false;
static uint32_t blePin = 0;
static char flashCmd[32] = {0};
static uint32_t flashUntil = 0;

void uiSetBleStatus(bool connected, uint32_t pin) {
    bleState = connected;
    blePin = pin;
}

void uiFlashCommand(const char *cmd) {
    strncpy(flashCmd, cmd, sizeof(flashCmd) - 1);
    flashCmd[sizeof(flashCmd) - 1] = 0;
    flashUntil = millis() + 3000;
}
```

Und in `uiTick` im Sekunden-Block nach der bestehenden Statuszeile ergänzen:

```cpp
        // Zeile 2: BLE-Zustand / letztes Kommando
        gfx->fillRect(0, 20, 160, 14, RGB565_BLACK);
        gfx->setCursor(4, 22);
        gfx->setTextSize(1);
        if (now < flashUntil && flashCmd[0]) {
            gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
            gfx->print("> ");
            gfx->print(flashCmd);
        } else if (bleState) {
            gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
            gfx->print("BLE verbunden");
        } else {
            gfx->setTextColor(RGB565_CYAN, RGB565_BLACK);
            gfx->print("BLE PIN ");
            gfx->print(blePin);
        }
```

- [ ] **Step 5: `main.cpp` final verdrahten**

Komplette neue `src/main.cpp`:

```cpp
#include <Arduino.h>
#include "esp_random.h"
#include <FastLED.h>
#include "jiggler.h"
#include "macro_engine.h"
#include "usb_hid.h"
#include "ble_remote.h"
#include "display_ui.h"

#define PIN_BUTTON 0
#define PIN_LED_DI 40
#define PIN_LED_CI 39
#define LONG_PRESS_MS 800

Jiggler jiggler;
CRGB statusLed;

static uint32_t btnDownAt = 0;
static bool longFired = false;
static uint32_t blePin = 0;

// ---- Makros (fest in Firmware) ----
static MacroStep m1steps[] = {
    {0, {ActionType::Combo, "", {0x83, 0, 0, 0}, 1, 'L'}}, // Bildschirm sperren
};
static MacroStep m2steps[] = {
    {0, {ActionType::Combo, "", {0x80, 0x81, 0, 0}, 2, 0xB1}}, // Task-Manager
};
static MacroStep m3steps[] = {
    {0, {ActionType::TypeText, "Hallo vom T-Dongle", {0, 0, 0, 0}, 0, 0}},
    {200, {ActionType::Combo, "", {0, 0, 0, 0}, 0, 0xB0}}, // ENTER
};
static Macro macros[] = {{m1steps, 1}, {m2steps, 1}, {m3steps, 2}};
MacroEngine engine(macros, 3);

static void setStatusLed(bool active) {
    statusLed = active ? CRGB(0, 60, 0) : CRGB::Black;
    FastLED.show();
}

static void dispatch(const Action &a, void *) {
    switch (a.type) {
        case ActionType::TypeText:
            usbHidTypeText(a.text);
            break;
        case ActionType::Combo:
            usbHidCombo(a.mods, a.modCount, a.key);
            break;
        case ActionType::ToggleJiggler:
            jiggler.toggle(millis());
            setStatusLed(jiggler.active());
            break;
        case ActionType::Error:
            break; // Grund geht als ERR über handleLine-Status ans Handy
    }
}

static void onBleLine(const char *line, void *) {
    uiFlashCommand(line);
    bool ok = engine.handleLine(millis(), line);
    bleSend(ok ? "OK" : "ERR");
}

void setup() {
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    usbHidBegin();

    FastLED.addLeds<APA102, PIN_LED_DI, PIN_LED_CI, BGR>(&statusLed, 1);

    engine.begin(dispatch, nullptr);

    blePin = 100000 + (esp_random() % 900000);
    bleBegin(blePin, onBleLine, nullptr);

    uiBegin();
    uiSetBleStatus(false, blePin);
    jiggler.begin(millis(), (uint32_t)esp_random());
    setStatusLed(jiggler.active());
}

void loop() {
    uint32_t now = millis();

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

    engine.tick(now);

    static bool lastBle = false;
    bool ble = bleConnected();
    if (ble != lastBle) {
        lastBle = ble;
        uiSetBleStatus(ble, blePin);
    }

    uiTick(now, jiggler.active(), jiggler.msUntilNext(now) / 1000, jiggler.count());
    delay(2);
}
```

- [ ] **Step 6: Build**

```bash
cmd //c 'tools\pio-win.bat run -e T-Dongle-S3'
```

Erwartet: `SUCCESS`. Bei NimBLE-2.x-API-Abweichungen (Callback-Signaturen, `NIMBLE_PROPERTY`-Namen) gegen die Header in `.pio/libdeps/T-Dongle-S3/NimBLE-Arduino/src/` korrigieren.

- [ ] **Step 7: Native Tests absichern (weiterhin grün)**

```bash
cmd //c 'set PATH=%CD%\tools\shims;%PATH% && set MSYSTEM= && .venv\Scripts\pio.exe test -e native'
```

Erwartet: `12 test cases: 12 succeeded` (5 Jiggler + 7 macro_engine)

- [ ] **Step 8: Flashen (Download-Modus)** — wie Task 2 Step 5

- [ ] **Step 9: Handy-Funktionstest durch Benutzer**

1. BLE-Terminal-App öffnen (z.B. „Serial Bluetooth Terminal" oder „nRF Connect")
2. Mit `USB Receiver` koppeln → Display zeigt `BLE PIN xxxxxx` → PIN am Handy eingeben
3. Service `6E400001…` öffnen, RX `…0002…`: Kommandos senden (Editor am PC offen):
   - `T hallo` → Text erscheint am PC
   - `K GUI+L` → Bildschirm sperrt sich (danach entsperren)
   - `M3` → tippt `Hallo vom T-Dongle` + Enter
   - `J` → LED/Jiggler toggelt
4. Display zeigt `BLE verbunden` bzw. kurz das Kommando an

- [ ] **Step 10: README um BLE-Nutzung ergänzen**

Abschnitt anhängen:

````markdown
## BLE-Fernbedienung

Das Gerät wirbt als `USB Receiver` (BLE). Beim Koppeln zeigt das Display
eine 6-stellige PIN (neu bei jedem Neustart).

Kommandos (zeilenbasiert, an RX `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`):

| Kommando | Wirkung |
|---|---|
| `T <text>` | Text tippen (ASCII/US-Layout) |
| `K <MOD+TASTE>` | Kürzel, z.B. `K GUI+L`, `K STRG+ALT+ENTF` |
| `M1`..`M3` | Makro: sperren / Task-Manager / Demo-Text |
| `J` | Jiggler AN/PAUSE |

Antwort: `OK` oder `ERR` auf TX `6E400003-…` (Notify).
Empfohlene App: „Serial Bluetooth Terminal" (frei belegbare Buttons).
````

- [ ] **Step 11: Commit**

```bash
git add src/ble_remote.h src/ble_remote.cpp src/display_ui.h src/display_ui.cpp src/main.cpp platformio.ini README.md
git commit -m "feat: BLE-Fernbedienung (NimBLE GATT-UART, Passkey via Display), Makros M1-M3, Display-BLE-Status"
```

---

## Self-Review Ergebnis

- Spec-Abdeckung: Composite-HID + Unifying-Tarnung (Task 2), GATT-UART + Passkey-Bonding (Task 3), Protokoll T/K/M/J (Task 1 Parser + Task 3 Verdrahtung), Makros fest in Firmware, nicht-blockierend, busy-Ablehnung (Task 1), Display BLE/PIN/Kommando (Task 3), Jiggler unverändert (Tasks 2+3 verwenden ihn unverändert), native Tests (Task 1, abgesichert in Task 3 Step 7), PnP-Verifizierung (Task 2 Step 6), Handy-Test (Task 3 Step 9), README (Task 3 Step 10). Alle Spec-Punkte abgedeckt.
- Keine Platzhalter; Code vollständig.
- Interface-Konsistenz: `usbHidBegin/TypeText/Combo/MouseMove` (Task 2 → Task 3), `bleBegin/bleSend/bleConnected` + `uiSetBleStatus/uiFlashCommand` (Task 3 intern konsistent), `MacroEngine::{begin,handleLine,busy,tick}` (Task 1 → Task 3 identisch). Action-Layout in Task-1-Tests entspricht der Initialisierung in Task-3-Makrotabelle (Reihenfolge type/text/mods/modCount/key).
