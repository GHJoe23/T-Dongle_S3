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
        digitalWrite(PIN_TFT_BL, HIGH); // Backlight AUS (aktiv LOW)
        prevX = -1;
    } else {
        gfx->fillScreen(RGB565_BLACK);
        digitalWrite(PIN_TFT_BL, LOW); // Backlight AN
        lastTextSec = 0xFFFFFFFFUL;
        prevX = -1;
    }
}

bool uiIsStealth() {
    return stealthMode;
}

static void drawMouse(int16_t x, int16_t y, uint16_t color) {
    gfx->fillCircle(x, y + 4, 6, color);     // Körper
    gfx->fillCircle(x - 4, y - 2, 3, color); // Ohr links
    gfx->fillCircle(x + 4, y - 2, 3, color); // Ohr rechts
}

void uiBegin(bool stealth) {
    gfx->begin();
    gfx->fillScreen(RGB565_BLACK);
    pinMode(PIN_TFT_BL, OUTPUT);
    uiSetStealth(stealth);
}

void uiJiggle(uint32_t now) {
    hopUntil = now + HOP_MS;
}

void uiShowBootloaderScreen() {
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
    if (stealthMode) return;

    // Animation, ~30 fps: Maus läuft ping-pong über den Schirm
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

    // Statuszeilen, 1x pro Sekunde (oder bei Statuswechsel)
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

        // Zeile 2: BLE-Zustand / PIN / Layout / letztes Kommando
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
            gfx->setTextColor(0xFD20 /* Orange */, RGB565_BLACK);
            gfx->print(currentLayout == KeyboardLayout::DE_QWERTZ ? "[DE]" : "[US]");
            const char *mName = (currentMode == JiggleMode::INSTANT_RETURN) ? " [0px]"
                              : (currentMode == JiggleMode::NATURAL_ARC)    ? " [Arc]"
                                                                            : " [PP]";
            gfx->print(mName);
        }
    }
}
