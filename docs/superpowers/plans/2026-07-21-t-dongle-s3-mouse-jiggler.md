# T-Dongle-S3 Mouse-Jiggler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Getarnter USB-Mouse-Jiggler für den LILYGO T-Dongle-S3: reines HID-Maus-Gerät mit Logitech-Identität, subtiler Zufalls-Jiggle ohne Zeigerdrift, Maus-Animation + Status auf dem ST7735-Display.

**Architecture:** Arduino-ESP32 Core 3.3.x (pioarduino 55.03.39) mit eingebautem TinyUSB-Stack (`USB`, `USBHIDMouse`, Deskriptoren via `USB.VID()/PID()/manufacturerName()/productName()`), Display via Arduino_GFX, Status-LED (APA102) via FastLED. Reine Jiggle-Logik als host-testbares Modul (pio native tests).

**Tech Stack:** PlatformIO Core (venv), pioarduino espressif32 55.03.39 (Arduino 3.3.9 / IDF 5.5.4), Arduino_GFX, FastLED, Unity (pio native).

**Spec:** `docs/superpowers/specs/2026-07-21-t-dongle-s3-mouse-jiggler-design.md`

## Global Constraints

- Board: LILYGO T-Dongle-S3, ESP32-S3, 16 MB QIO-Flash, **kein PSRAM**
- USB-Modus: `ARDUINO_USB_MODE=0` (USB-OTG/TinyUSB), `ARDUINO_USB_CDC_ON_BOOT=0` → kein COM-Port
- USB-Identität: VID `0x046D`, PID `0xC077`, Manufacturer `Logitech`, Product `USB Optical Mouse`
- Pins: TFT MOSI=3, SCK=5, CS=4, DC=2, RST=1, BL=38 (aktiv LOW); Taster=GPIO 0 (aktiv LOW); APA102 DI=40, CI=39 (BGR)
- Jiggle: Intervall zufällig 30–60 s, Bewegung 1–4 px, jede Bewegung wird beim nächsten Jiggle invertiert (keine Drift)
- Arduino Core >= 3.3.0 (wegen USB-Deskriptor-API); pioarduino-Plattform-URL:
  `https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip`
- Toolchain nur im Projekt-venv (`.venv/`), keine Systeminstallation
- Windows-Hinweis: `curl` auf diesem Rechner braucht `--ssl-no-revoke`; pip funktioniert normal

---

### Task 1: Toolchain & Projektgerüst

**Files:**
- Create: `boards/dongles3.json`
- Create: `platformio.ini`
- Create: `src/main.cpp`
- Create: `README.md`
- Create: `.gitignore`

**Interfaces:**
- Produces: Build-Umgebung (`pio run` im venv), Board-Definition `dongles3` mit TinyUSB-Flags; minimale Firmware, die das Display initialisiert (Nachweis, dass Pins/Treiber stimmen).

- [ ] **Step 1: venv + PlatformIO installieren**

```bash
python -m venv .venv
./.venv/Scripts/pip install -U platformio
./.venv/Scripts/pio --version
```

Erwartet: `PlatformIO Core, version 6.x`

- [ ] **Step 2: Board-Definition `boards/dongles3.json` anlegen**

Basiert auf der LILYGO-Definition, aber mit TinyUSB-Flags (`ARDUINO_USB_MODE=0`, `ARDUINO_USB_CDC_ON_BOOT=0`) statt Hardware-CDC:

```json
{
    "build": {
        "arduino": {
            "partitions": "default_16MB.csv"
        },
        "core": "esp32",
        "extra_flags": [
            "-DARDUINO_RUNNING_CORE=1",
            "-DARDUINO_EVENT_RUNNING_CORE=1",
            "-DARDUINO_DONGLES3",
            "-DARDUINO_USB_MODE=0",
            "-DARDUINO_USB_CDC_ON_BOOT=0"
        ],
        "f_cpu": "240000000L",
        "f_flash": "80000000L",
        "flash_mode": "qio",
        "hwids": [
            [
                "0x303A",
                "0x82C1"
            ],
            [
                "0x303A",
                "0x1001"
            ]
        ],
        "mcu": "esp32s3",
        "variant": "esp32s3"
    },
    "connectivity": [
        "bluetooth",
        "wifi"
    ],
    "frameworks": [
        "arduino",
        "espidf"
    ],
    "name": "LilyGo T-Dongle S3 (16 MB QD, No PSRAM)",
    "upload": {
        "flash_size": "16MB",
        "maximum_ram_size": 327680,
        "maximum_size": 16777216,
        "require_upload_port": true,
        "speed": 921600
    },
    "url": "https://lilygo.cc/products/t-dongle-s3",
    "vendor": "LilyGo"
}
```

- [ ] **Step 3: `platformio.ini` anlegen**

```ini
[env:T-Dongle-S3]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
framework = arduino
board = dongles3
upload_speed = 921600
build_flags =
    -DCORE_DEBUG_LEVEL=0
lib_deps =
    moononournation/Arduino_GFX@^1.5.0
    fastled/FastLED@^3.9.0

[env:native]
platform = native
build_flags = -std=gnu++14
```

- [ ] **Step 4: Minimale `src/main.cpp` (Display-Selbsttest)**

```cpp
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

static Arduino_DataBus *bus = new Arduino_ESP32SPI(2 /* DC */, 4 /* CS */, 5 /* SCK */, 3 /* MOSI */, GFX_NOT_DEFINED);
static Arduino_ST7735 *gfx = new Arduino_ST7735(bus, 1 /* RST */, 1 /* Rotation */, true /* IPS */,
                                                80, 160, 26, 1, 26, 1);

void setup() {
    gfx->begin();
    gfx->fillScreen(BLACK);
    gfx->setCursor(10, 30);
    gfx->setTextSize(2);
    gfx->setTextColor(GREEN, BLACK);
    gfx->print("Jiggler");
    pinMode(38, OUTPUT);
    digitalWrite(38, LOW); // Backlight an (aktiv LOW)
}

void loop() {
}
```

- [ ] **Step 5: Build ausführen (lädt Plattform + Toolchain, dauert beim ersten Mal mehrere Minuten)**

```bash
./.venv/Scripts/pio run
```

Erwartet: `SUCCESS`, keine Fehler. Falls `Arduino_ESP32SPI`-Signatur in der installierten Arduino_GFX-Version abweicht, an die Header-Datei unter `.pio/libdeps/T-Dongle-S3/Arduino_GFX/src/databus/Arduino_ESP32SPI.h` anpassen.

- [ ] **Step 6: `.gitignore` und `README.md` anlegen**

`.gitignore`:

```
.venv/
.pio/
```

`README.md`:

````markdown
# T-Dongle-S3 Mouse Jiggler

Getarnter USB-Mouse-Jiggler für LILYGO T-Dongle-S3. Meldet sich als
HID-Maus (VID 0x046D / PID 0xC077), ohne COM-Port.

## Build & Flash

```bash
python -m venv .venv
./.venv/Scripts/pip install -U platformio
./.venv/Scripts/pio run
./.venv/Scripts/pio run -t upload --upload-port COMx
```

## Reflash nach eigener Firmware

Die Firmware blendet den COM-Port aus. Zum Reflashen:
BOOT-Taster gedrückt halten, Gerät einstecken, Taster loslassen
(Download-Modus, erscheint als „USB JTAG/serial debug unit"),
dann `pio run -t upload --upload-port COMx`. Danach aus- und wieder
einstecken, ohne BOOT zu drücken.

## Bedienung

- Kurzer Druck auf BOOT: Jiggler AN/PAUSE (LED grün = aktiv)
- Display: Maus-Animation, Status, Countdown, Jiggle-Zähler
````

- [ ] **Step 7: Commit**

```bash
git add boards/dongles3.json platformio.ini src/main.cpp README.md .gitignore
git commit -m "feat: Projektgeruest, Toolchain, Display-Selbsttest"
```

---

### Task 2: Jiggle-Logik (TDD, native Tests)

**Files:**
- Create: `src/jiggler.h`
- Create: `src/jiggler.cpp`
- Create: `test/test_jiggler/test_jiggler.cpp`

**Interfaces:**
- Produces (wird in Task 3/4 konsumiert):

```cpp
class Jiggler {
public:
  void begin(uint32_t now, uint32_t seed);            // startet aktiv
  void toggle(uint32_t now);                          // AN <-> PAUSE
  bool active() const;
  bool poll(uint32_t now, int8_t &dx, int8_t &dy);    // true = Jiggle fällig
  uint32_t msUntilNext(uint32_t now) const;           // 0 wenn pausiert
  uint32_t count() const;                             // gesendete Jiggles
};
```

- [ ] **Step 1: Failing Tests schreiben**

`test/test_jiggler/test_jiggler.cpp`:

```cpp
#include <unity.h>
#include "jiggler.h"

void test_not_due_before_min_interval() {
    for (uint32_t seed = 1; seed <= 5; seed++) {
        Jiggler j;
        j.begin(0, seed);
        int8_t dx = 0, dy = 0;
        TEST_ASSERT_FALSE(j.poll(29999, dx, dy)); // Intervall min. 30000 ms
    }
}

void test_due_by_max_interval_and_movement_small() {
    Jiggler j;
    j.begin(0, 42);
    int8_t dx = 0, dy = 0;
    TEST_ASSERT_TRUE(j.poll(60001, dx, dy)); // Intervall max. 60000 ms
    TEST_ASSERT_INT8_WITHIN(4, 0, dx);
    TEST_ASSERT_INT8_WITHIN(4, 0, dy);
    TEST_ASSERT_FALSE(dx == 0 && dy == 0);
    TEST_ASSERT_EQUAL_UINT32(1, j.count());
}

void test_second_jiggle_inverts_first() {
    Jiggler j;
    j.begin(0, 42);
    int8_t dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    TEST_ASSERT_TRUE(j.poll(60001, dx1, dy1));
    TEST_ASSERT_TRUE(j.poll(130000, dx2, dy2));
    TEST_ASSERT_EQUAL_INT8(-dx1, dx2);
    TEST_ASSERT_EQUAL_INT8(-dy1, dy2);
}

void test_toggle_pauses_and_resumes() {
    Jiggler j;
    j.begin(0, 7);
    int8_t dx = 0, dy = 0;
    j.toggle(1000);
    TEST_ASSERT_FALSE(j.active());
    TEST_ASSERT_FALSE(j.poll(1000000, dx, dy));
    TEST_ASSERT_EQUAL_UINT32(0, j.msUntilNext(2000));
    j.toggle(2000);
    TEST_ASSERT_TRUE(j.active());
}

void test_countdown_decreases() {
    Jiggler j;
    j.begin(0, 7);
    uint32_t c1 = j.msUntilNext(0);
    uint32_t c2 = j.msUntilNext(5000);
    TEST_ASSERT_TRUE(c1 > c2);
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_not_due_before_min_interval);
    RUN_TEST(test_due_by_max_interval_and_movement_small);
    RUN_TEST(test_second_jiggle_inverts_first);
    RUN_TEST(test_toggle_pauses_and_resumes);
    RUN_TEST(test_countdown_decreases);
    return UNITY_END();
}
```

- [ ] **Step 2: Tests laufen lassen → müssen fehlschlagen**

```bash
./.venv/Scripts/pio test -e native
```

Erwartet: FAIL (jiggler.h nicht gefunden / Linkerfehler)

- [ ] **Step 3: Implementierung**

`src/jiggler.h`:

```cpp
#pragma once
#include <stdint.h>

class Jiggler {
public:
    void begin(uint32_t now, uint32_t seed);
    void toggle(uint32_t now);
    bool active() const;
    bool poll(uint32_t now, int8_t &dx, int8_t &dy);
    uint32_t msUntilNext(uint32_t now) const;
    uint32_t count() const;

private:
    uint32_t _next = 0;
    uint32_t _rng = 1;
    uint32_t _count = 0;
    bool _active = false;
    bool _pendingReturn = false;
    int8_t _lastDx = 0;
    int8_t _lastDy = 0;

    uint32_t _rand();
    uint32_t _interval();
};
```

`src/jiggler.cpp`:

```cpp
#include "jiggler.h"

uint32_t Jiggler::_rand() {
    uint32_t x = _rng; // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    _rng = x;
    return x;
}

uint32_t Jiggler::_interval() {
    return 30000UL + (_rand() % 30001UL); // 30..60 s
}

void Jiggler::begin(uint32_t now, uint32_t seed) {
    _rng = seed ? seed : 0x9E3779B9UL;
    _active = true;
    _count = 0;
    _pendingReturn = false;
    _lastDx = _lastDy = 0;
    _next = now + _interval();
}

void Jiggler::toggle(uint32_t now) {
    _active = !_active;
    if (_active) {
        _next = now + 2000UL; // kurzer Vorlauf nach Reaktivierung
    }
}

bool Jiggler::active() const {
    return _active;
}

bool Jiggler::poll(uint32_t now, int8_t &dx, int8_t &dy) {
    if (!_active || (int32_t)(now - _next) < 0) {
        return false;
    }
    if (_pendingReturn) {
        dx = -_lastDx;
        dy = -_lastDy;
    } else {
        do {
            dx = (int8_t)((int32_t)(_rand() % 9) - 4);
            dy = (int8_t)((int32_t)(_rand() % 9) - 4);
        } while (dx == 0 && dy == 0);
        _lastDx = dx;
        _lastDy = dy;
    }
    _pendingReturn = !_pendingReturn;
    _count++;
    _next = now + _interval();
    return true;
}

uint32_t Jiggler::msUntilNext(uint32_t now) const {
    if (!_active || (int32_t)(_next - now) <= 0) {
        return 0;
    }
    return _next - now;
}
```

- [ ] **Step 4: Tests laufen lassen → müssen grün sein**

```bash
./.venv/Scripts/pio test -e native
```

Erwartet: `5 Tests 0 Failures`

- [ ] **Step 5: Target-Build absichern (Modul kompiliert auch für ESP32)**

```bash
./.venv/Scripts/pio run
```

Erwartet: `SUCCESS`

- [ ] **Step 6: Commit**

```bash
git add src/jiggler.h src/jiggler.cpp test/test_jiggler/test_jiggler.cpp
git commit -m "feat: Jiggle-Logik mit nativen Unit-Tests (TDD)"
```

---

### Task 3: USB-Tarnung + HID + Jiggler + Taster + LED

**Files:**
- Modify: `src/main.cpp` (komplett ersetzen)

**Interfaces:**
- Consumes: `Jiggler` aus Task 2
- Produces: lauffähige Firmware; Gerät enumeriert als reine HID-Maus mit Logitech-Identität

- [ ] **Step 1: `src/main.cpp` ersetzen**

```cpp
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
```

- [ ] **Step 2: Build**

```bash
./.venv/Scripts/pio run
```

Erwartet: `SUCCESS`

- [ ] **Step 3: Flashen (Werks-Firmware ist noch auf COM3)**

```bash
./.venv/Scripts/pio run -t upload --upload-port COM3
```

Erwartet: `Writing at 0x00010000...`, `Hard resetting via RTS pin...` bzw. Abschluss ohne Fehler. Falls der Auto-Reset nicht greift: BOOT gedrückt halten, Upload starten, Taster loslassen.

- [ ] **Step 4: Tarnung Windows-seitig verifizieren**

Gerät einmal aus- und einstecken, dann:

```bash
powershell.exe -NoProfile -Command 'Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -like "*VID_046D&PID_C077*" } | Select-Object Name, DeviceID | Format-List'
```

Erwartet: `USB-Eingabegerät` mit `USB\VID_046D&PID_C077...` (plus zugehöriger HID-Eintrag).

```bash
powershell.exe -NoProfile -Command 'Get-CimInstance Win32_PnPEntity | Where-Object { $_.DeviceID -like "*VID_CAFE*" -or $_.DeviceID -like "*VID_303A*" } | Select-Object Name, DeviceID'
```

Erwartet: **keine** Treffer (kein COM-Port, kein Verbundgerät, kein TinyUSB-Rest).

- [ ] **Step 5: Funktionstest durch Benutzer**

- Cursor bewegt sich innerhalb von max. 60 s minimal (wenige Pixel) und springt nicht dauerhaft weg
- Kurzer Tastendruck: LED aus → keine Bewegungen mehr; nochmal drücken: LED grün → Bewegungen wieder da

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "feat: USB-Tarnung (Logitech HID-Maus), Jiggle, Taster, Status-LED"
```

---

### Task 4: Display-Animation + Statuszeile

**Files:**
- Create: `src/display_ui.h`
- Create: `src/display_ui.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Produces (von `main.cpp` konsumiert):

```cpp
void uiBegin();                                                              // Display + Backlight
void uiJiggle(uint32_t now);                                                 // Hüpf-Animation triggern
void uiTick(uint32_t now, bool active, uint32_t countdownSec, uint32_t jiggleCount);
```

- [ ] **Step 1: `src/display_ui.h` anlegen**

```cpp
#pragma once
#include <stdint.h>

void uiBegin();
void uiJiggle(uint32_t now);
void uiTick(uint32_t now, bool active, uint32_t countdownSec, uint32_t jiggleCount);
```

- [ ] **Step 2: `src/display_ui.cpp` anlegen**

```cpp
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "display_ui.h"

#define PIN_TFT_DC 2
#define PIN_TFT_CS 4
#define PIN_TFT_SCK 5
#define PIN_TFT_MOSI 3
#define PIN_TFT_RST 1
#define PIN_TFT_BL 38 // aktiv LOW

static Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_SCK, PIN_TFT_MOSI, GFX_NOT_DEFINED);
static Arduino_ST7735 *gfx = new Arduino_ST7735(bus, PIN_TFT_RST, 1 /* Rotation */, true /* IPS */,
                                                80, 160, 26, 1, 26, 1);

static const int16_t MOUSE_Y = 52;
static const int16_t MOUSE_MIN_X = 10;
static const int16_t MOUSE_MAX_X = 150;
static const int16_t HOP_HEIGHT = 12;
static const uint32_t HOP_MS = 300;

static int16_t mouseX = MOUSE_MIN_X;
static int8_t mouseDir = 1;
static int16_t prevX = -1;
static int16_t prevY = -1;
static uint32_t lastAnimMs = 0;
static uint32_t hopUntil = 0;
static uint32_t lastTextSec = 0xFFFFFFFFUL;

static void drawMouse(int16_t x, int16_t y, uint16_t color) {
    gfx->fillCircle(x, y + 4, 6, color);     // Körper
    gfx->fillCircle(x - 4, y - 2, 3, color); // Ohr links
    gfx->fillCircle(x + 4, y - 2, 3, color); // Ohr rechts
}

void uiBegin() {
    gfx->begin();
    gfx->fillScreen(BLACK);
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, LOW); // Backlight an
}

void uiJiggle(uint32_t now) {
    hopUntil = now + HOP_MS;
}

void uiTick(uint32_t now, bool active, uint32_t countdownSec, uint32_t jiggleCount) {
    // Animation, ~30 fps: Maus läuft ping-pong über den Schirm
    if (now - lastAnimMs >= 33) {
        lastAnimMs = now;
        if (prevX >= 0) {
            drawMouse(prevX, prevY, BLACK); // alte Position löschen
        }
        mouseX += mouseDir * 2;
        if (mouseX >= MOUSE_MAX_X) {
            mouseX = MOUSE_MAX_X;
            mouseDir = -1;
        }
        if (mouseX <= MOUSE_MIN_X) {
            mouseX = MOUSE_MIN_X;
            mouseDir = 1;
        }
        int16_t y = (now < hopUntil) ? MOUSE_Y - HOP_HEIGHT : MOUSE_Y;
        drawMouse(mouseX, y, WHITE);
        prevX = mouseX;
        prevY = y;
    }

    // Statuszeile, 1x pro Sekunde
    if (now / 1000 != lastTextSec) {
        lastTextSec = now / 1000;
        gfx->fillRect(0, 0, 160, 20, BLACK);
        gfx->setCursor(4, 4);
        gfx->setTextSize(2);
        gfx->setTextColor(active ? GREEN : RED, BLACK);
        gfx->print(active ? "AN " : "PAUSE");
        if (active) {
            gfx->setTextColor(WHITE, BLACK);
            gfx->print(countdownSec);
            gfx->print("s #");
            gfx->print(jiggleCount);
        }
    }
}
```

- [ ] **Step 3: `src/main.cpp` um Display-Ansteuerung ergänzen**

Include ergänzen:

```cpp
#include "display_ui.h"
```

In `setup()` vor `jiggler.begin(...)` einfügen:

```cpp
    uiBegin();
```

In `loop()` den Jiggle-Block ersetzen durch:

```cpp
    int8_t dx, dy;
    if (jiggler.poll(now, dx, dy)) {
        Mouse.move(dx, dy);
        uiJiggle(now);
    }

    uiTick(now, jiggler.active(), jiggler.msUntilNext(now) / 1000, jiggler.count());
```

- [ ] **Step 4: Build**

```bash
./.venv/Scripts/pio run
```

Erwartet: `SUCCESS`

- [ ] **Step 5: Flashen (ab jetzt: Download-Modus nötig!)**

1. BOOT-Taster am Dongle gedrückt halten, Gerät einstecken, Taster loslassen
2. Neuer COM-Port ermitteln:

```bash
powershell.exe -NoProfile -Command 'Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match "COM\d" } | Select-Object Name, DeviceID'
```

3. Flashen (COMx = ermittelter Port):

```bash
./.venv/Scripts/pio run -t upload --upload-port COMx
```

4. Gerät aus- und wieder einstecken (ohne BOOT)

- [ ] **Step 6: Visueller Test durch Benutzer**

- Maus läuft über das Display, hüpft bei jedem Jiggle
- Statuszeile: `AN <Countdown>s #<Zähler>`, bei Tastendruck `PAUSE` in Rot
- Falls Bild gespiegelt/kopfüber: Rotation in `src/display_ui.cpp` von `1` auf `3` ändern, neu flashen

- [ ] **Step 7: Commit**

```bash
git add src/display_ui.h src/display_ui.cpp src/main.cpp
git commit -m "feat: Display-Animation (Maus-Sprite) und Statuszeile"
```

---

## Self-Review Ergebnis

- Spec-Abdeckung: USB-Tarnung (Task 3), Jiggle ohne Drift (Task 2+3), Taster (Task 3), LED (Task 3), Display/Animation/Status (Task 4), Toolchain im venv (Task 1), Flash-/Recovery-Doku (Task 1 README, Task 4 Step 5), Windows-Verifizierung (Task 3 Step 4). Alle Spec-Punkte abgedeckt.
- Keine Platzhalter; jeder Code-Schritt enthält vollständigen Code.
- Interface-Namen konsistent: `Jiggler::{begin,toggle,active,poll,msUntilNext,count}`, `uiBegin/uiJiggle/uiTick` in Tasks 2–4 identisch verwendet.
