#include <Arduino.h>
#include <Arduino_GFX_Library.h>

static Arduino_DataBus *bus = new Arduino_ESP32SPI(2 /* DC */, 4 /* CS */, 5 /* SCK */, 3 /* MOSI */, GFX_NOT_DEFINED);
static Arduino_ST7735 *gfx = new Arduino_ST7735(bus, 1 /* RST */, 1 /* Rotation */, true /* IPS */,
                                                80, 160, 26, 1, 26, 1);

void setup() {
    gfx->begin();
    gfx->fillScreen(RGB565_BLACK);
    gfx->setCursor(10, 30);
    gfx->setTextSize(2);
    gfx->setTextColor(RGB565_GREEN, RGB565_BLACK);
    gfx->print("Jiggler");
    pinMode(38, OUTPUT);
    digitalWrite(38, LOW); // Backlight an (aktiv LOW)
}

void loop() {
}
