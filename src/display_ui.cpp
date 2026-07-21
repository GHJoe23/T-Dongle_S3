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
    gfx->begin(); // 40 MHz (Arduino_GFX-Default) — am Gerät verifiziert
    gfx->fillScreen(RGB565_BLACK);
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
            drawMouse(prevX, prevY, RGB565_BLACK); // alte Position löschen
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
        drawMouse(mouseX, y, RGB565_WHITE);
        prevX = mouseX;
        prevY = y;
    }

    // Statuszeile, 1x pro Sekunde
    if (now / 1000 != lastTextSec) {
        lastTextSec = now / 1000;
        gfx->fillRect(0, 0, 160, 20, RGB565_BLACK);
        gfx->setCursor(4, 4);
        gfx->setTextSize(2);
        gfx->setTextColor(active ? RGB565_GREEN : RGB565_RED, RGB565_BLACK);
        gfx->print(active ? "AN " : "PAUSE");
        if (active) {
            gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
            gfx->print(countdownSec);
            gfx->print("s #");
            gfx->print(jiggleCount);
        }
    }
}
