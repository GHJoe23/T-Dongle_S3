#include <Arduino.h>
#include <string.h>
#include "display_ui.h"

#if defined(ARDUINO_ARCH_NRF52)
// ============================================================================
// Seeed Studio XIAO nRF52840 Sense: Onboard 3-in-1 RGB-LED (aktiv LOW)
// ============================================================================
static bool stealthMode = false;
static bool bleConnectedState = false;
static uint32_t jigglePulseUntil = 0;
static uint32_t cmdPulseUntil = 0;
static uint32_t lastLedUpdateMs = 0;

static void setRgbLeds(bool rOn, bool gOn, bool bOn) {
    digitalWrite(LED_RED, rOn ? LOW : HIGH);
    digitalWrite(LED_GREEN, gOn ? LOW : HIGH);
    digitalWrite(LED_BLUE, bOn ? LOW : HIGH);
}

void uiBegin(bool stealth) {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(LED_BLUE, OUTPUT);
    setRgbLeds(false, false, false);
    uiSetStealth(stealth);
}

void uiTick(uint32_t now, bool active, uint32_t countdownSec, uint32_t jiggleCount) {
    (void)countdownSec;
    (void)jiggleCount;

    if (stealthMode) {
        setRgbLeds(false, false, false);
        return;
    }

    if (now - lastLedUpdateMs < 25) return;
    lastLedUpdateMs = now;

    if (now < cmdPulseUntil) {
        setRgbLeds(false, true, true);
        return;
    }

    if (now < jigglePulseUntil) {
        setRgbLeds(false, true, false);
        return;
    }

    uint32_t phase = now % 3000;
    if (active) {
        bool greenHeartbeat = (phase < 90);
        bool blueHeartbeat = bleConnectedState && (phase >= 180 && phase < 250);
        setRgbLeds(false, greenHeartbeat, blueHeartbeat);
    } else {
        bool redHeartbeat = (phase < 70);
        bool blueHeartbeat = bleConnectedState && (phase >= 180 && phase < 250);
        setRgbLeds(redHeartbeat, false, blueHeartbeat);
    }
}

void uiJiggle(uint32_t now) {
    jigglePulseUntil = now + 120;
}

void uiSetBleStatus(bool connected, uint32_t pin) {
    (void)pin;
    bleConnectedState = connected;
}

void uiSetConfigInfo(KeyboardLayout layout, JiggleMode mode) {
    (void)layout;
    (void)mode;
}

void uiFlashCommand(const char *cmd) {
    (void)cmd;
    cmdPulseUntil = millis() + 90;
}

void uiSetStealth(bool stealth) {
    stealthMode = stealth;
    if (stealthMode) {
        setRgbLeds(false, false, false);
    }
}

bool uiIsStealth() {
    return stealthMode;
}

void uiShowBootloaderScreen() {
    setRgbLeds(false, false, true);
}

#else
// ============================================================================
// LilyGO T-Dongle S3: 0.96" ST7735 IPS-TFT (160x80) + APA102 RGB-LED
// ============================================================================
#include <Arduino_GFX_Library.h>
#include <FastLED.h>

#define PIN_TFT_DC 2
#define PIN_TFT_CS 4
#define PIN_TFT_SCK 5
#define PIN_TFT_MOSI 3
#define PIN_TFT_RST 1
#define PIN_TFT_BL 38 // aktiv LOW
#define PIN_LED_DI 40
#define PIN_LED_CI 39

static Arduino_DataBus *bus = new Arduino_ESP32SPI(PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_SCK, PIN_TFT_MOSI, GFX_NOT_DEFINED);
static Arduino_ST7735 *gfx = new Arduino_ST7735(bus, PIN_TFT_RST, 1 /* Rotation */, true /* IPS */,
                                                80, 160, 26, 1, 26, 1);
static CRGB statusLed;
static bool lastLedActive = false;
static bool ledInitialized = false;

static const int16_t MOUSE_Y = 54;
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

static bool bleState = false;
static uint32_t blePin = 400400;
static bool stealthMode = false;
static KeyboardLayout currentLayout = KeyboardLayout::DE_QWERTZ;
static JiggleMode currentMode = JiggleMode::PING_PONG;
static char flashCmd[28] = {0};
static uint32_t flashUntil = 0;

static void updateApa102Led(bool active) {
    if (!ledInitialized) return;
    if (stealthMode) {
        statusLed = CRGB::Black;
    } else if (active) {
        statusLed = CRGB(0, 50, 0); // Grün = Jiggler aktiv
    } else {
        statusLed = CRGB(20, 0, 0); // Schwaches Rot = Jiggler pausiert
    }
    FastLED.show();
}

void uiSetBleStatus(bool connected, uint32_t pin) {
    bleState = connected;
    blePin = pin;
    lastTextSec = 0xFFFFFFFFUL;
}

void uiSetConfigInfo(KeyboardLayout layout, JiggleMode mode) {
    currentLayout = layout;
    currentMode = mode;
    lastTextSec = 0xFFFFFFFFUL;
}

void uiFlashCommand(const char *cmd) {
    strncpy(flashCmd, cmd, sizeof(flashCmd) - 1);
    flashCmd[sizeof(flashCmd) - 1] = 0;
    flashUntil = millis() + 2500;
    lastTextSec = 0xFFFFFFFFUL;
}

void uiSetStealth(bool stealth) {
    stealthMode = stealth;
    if (stealthMode) {
        gfx->fillScreen(RGB565_BLACK);
        digitalWrite(PIN_TFT_BL, HIGH);
        prevX = -1;
    } else {
        gfx->fillScreen(RGB565_BLACK);
        digitalWrite(PIN_TFT_BL, LOW);
        lastTextSec = 0xFFFFFFFFUL;
        prevX = -1;
    }
    updateApa102Led(lastLedActive);
}

bool uiIsStealth() {
    return stealthMode;
}

static void drawMouse(int16_t x, int16_t y, uint16_t color) {
    gfx->fillCircle(x, y + 4, 6, color);
    gfx->fillCircle(x - 4, y - 2, 3, color);
    gfx->fillCircle(x + 4, y - 2, 3, color);
}

void uiBegin(bool stealth) {
    FastLED.addLeds<APA102, PIN_LED_DI, PIN_LED_CI, BGR>(&statusLed, 1);
    ledInitialized = true;
    gfx->begin();
    gfx->fillScreen(RGB565_BLACK);
    pinMode(PIN_TFT_BL, OUTPUT);
    uiSetStealth(stealth);
}

void uiJiggle(uint32_t now) {
    hopUntil = now + HOP_MS;
}

void uiShowBootloaderScreen() {
    statusLed = CRGB(0, 0, 90);
    FastLED.show();
    digitalWrite(PIN_TFT_BL, LOW);
    gfx->fillScreen(RGB565_BLUE);
    gfx->setTextColor(RGB565_WHITE, RGB565_BLUE);
    gfx->setTextSize(2);
    gfx->setCursor(10, 16);
    gfx->print("BOOTLOADER");
    gfx->setTextSize(1);
    gfx->setCursor(10, 46);
    gfx->print("Bereit zum Flashen...");
}

void uiTick(uint32_t now, bool active, uint32_t countdownSec, uint32_t jiggleCount) {
    if (active != lastLedActive) {
        lastLedActive = active;
        updateApa102Led(active);
    }
    if (stealthMode) return;

    if (now - lastAnimMs >= 33) {
        lastAnimMs = now;
        if (prevX >= 0) {
            drawMouse(prevX, prevY, RGB565_BLACK);
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
        drawMouse(mouseX, y, active ? RGB565_WHITE : 0x7BEF);
        prevX = mouseX;
        prevY = y;
    }

    if (now / 1000 != lastTextSec) {
        lastTextSec = now / 1000;
        gfx->fillRect(0, 0, 160, 20, RGB565_BLACK);
        gfx->setCursor(4, 3);
        gfx->setTextSize(2);
        gfx->setTextColor(active ? RGB565_GREEN : RGB565_RED, RGB565_BLACK);
        gfx->print(active ? "AN " : "PAUSE");
        if (active) {
            gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
            gfx->print(countdownSec);
            gfx->print("s #");
            gfx->print(jiggleCount);
        }

        gfx->fillRect(0, 20, 160, 14, RGB565_BLACK);
        gfx->setCursor(4, 22);
        gfx->setTextSize(1);
        if (now < flashUntil && flashCmd[0]) {
            gfx->setTextColor(RGB565_YELLOW, RGB565_BLACK);
            gfx->print("> ");
            gfx->print(flashCmd);
        } else {
            gfx->setTextColor(bleState ? RGB565_GREEN : RGB565_CYAN, RGB565_BLACK);
            if (bleState) {
                gfx->print("BLE OK ");
            } else {
                gfx->print("PIN:");
                gfx->print(blePin);
                gfx->print(" ");
            }
            gfx->setTextColor(0xFD20, RGB565_BLACK);
            gfx->print(currentLayout == KeyboardLayout::DE_QWERTZ ? "[DE]" : "[US]");
            const char *mName = (currentMode == JiggleMode::INSTANT_RETURN) ? " [0px]"
                              : (currentMode == JiggleMode::NATURAL_ARC)    ? " [Arc]"
                                                                            : " [PP]";
            gfx->print(mName);
        }
    }
}
#endif
