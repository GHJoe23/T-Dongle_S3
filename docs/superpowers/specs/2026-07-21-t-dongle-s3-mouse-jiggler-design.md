# Design: Getarnter USB-Mouse-Jiggler für LILYGO T-Dongle-S3

Datum: 2026-07-21
Status: freigegeben (Design-Phase abgeschlossen)

## Ziel

Ein USB-Mouse-Jiggler auf dem LILYGO T-Dongle-S3 (ESP32-S3, 0,96"-IPS-Display
ST7735, 160×80), der sich unter Windows als vertrauenswürdige Standard-Maus
ausgibt und eine Maus-Animation mit Status auf dem Display zeigt.

## Hardware-Fakten (verifiziert aus dem LILYGO-Repo)

Quelle: https://github.com/Xinyuan-LilyGO/T-Dongle-S3
(Doku: `docs/en/t-dongle-s3/REAMDE.MD`, Beispiele: `examples/`)

| Komponente | Details |
|---|---|
| SoC | ESP32-S3, 16 MB QIO-Flash, **kein PSRAM** |
| Display | ST7735 SPI, 160×80; MOSI=3, CLK=5, CS=4, DC=2, RST=1, Backlight=38 (aktiv LOW), Farbinvertierung an, Landscape: Gap (1,26), swap_xy, mirror(false,true) |
| Taster | BOOT = GPIO 0, aktiv LOW (gleichzeitig Download-Modus-Taster) |
| RGB-LED | APA102, Daten=40, Takt=39, Farbreihenfolge BGR |
| SD-Karte | SDMMC (D0=14, D1=17, D2=21, D3=18, CLK=12, CMD=16) – nicht verwendet |
| Board-Definition | `boards/dongles3.json` im LILYGO-Repo (16 MB, kein PSRAM, qio) |

Wichtiger Hinweis aus der LILYGO-Doku: Sobald die Firmware den USB-OTG-Modus
(TinyUSB) nutzt, muss das Gerät zum Reflashen manuell in den Download-Modus
versetzt werden (BOOT gedrückt halten beim Einstecken).

## Architektur

Ansatz A (freigegeben): Arduino-ESP32 Core 3.3.x mit eingebautem
TinyUSB-Stack (`USB`, `USBHIDMouse`) + Arduino_GFX für das Display.

- Der Core 3.3.x erlaubt das Setzen von VID/PID, Hersteller-, Produkt- und
  Seriennummer-Strings vor `USB.begin()`. LILYGOs eigenes Beispiel
  `examples/usb_hid_mouse` nutzt denselben Weg (erfordert Core >= 3.3.0 und
  USB-Modus „USB-OTG (TinyUSB)").
- Kein Adafruit-TinyUSB als Drittbibliothek nötig (war Alternative A1,
  Fallback falls die Core-eigene API nicht wie erwartet funktioniert).

### Module

1. **USB-Identität** (`usb_identity.*`): Setzt Deskriptoren
   (VID/PID/Strings einer handelsüblichen Maus, z. B. Logitech
   „USB Optical Mouse"), startet ausschließlich ein HID-Mouse-Interface.
   Kein CDC, kein MSC, kein JTAG → kein COM-Port im Gerätemanager.
2. **Jiggle-Logik** (`jiggler.*`): Zustandsmaschine AN/PAUSE.
   Zufallsintervall 30–60 s; Bewegung 1–4 px in zufälliger Richtung,
   gelegentlich als Zwei-Schritt. Jede Bewegung wird gemerkt und beim
   nächsten Jiggle invertiert → der Zeiger driftet netto nie weg.
   Relative Reports, keine Absolutpositionierung.
3. **Display/Animation** (`display.*`): Arduino_GFX, ST7735 landscape 160×80.
   Maus-Sprite läuft über den Bildschirm, „hüpft" beim Senden eines Jiggles.
   Statuszeile: Zustand (AN/PAUSE), Countdown bis nächster Jiggle,
   Jiggle-Zähler.
4. **Bedienung** (`main.cpp`): BOOT-Taster entprellt; kurzer Druck toggelt
   AN/PAUSE. APA102-LED: grün = aktiv, aus = Pause (FastLED oder direktes
   Bit-Banging; FastLED ist bereits für APA102 erprobt).

### Datenfluss

`loop()` → Taster-Entprellung → Jiggler-Zustand aktualisieren → bei
Fälligkeit `Mouse.move(dx, dy)` → Display-Redraw (Animation + Status) →
LED-Status setzen.

## Toolchain

- PlatformIO Core (CLI) in einem Python-venv im Projektordner
  (`.venv/`), keine Systeminstallation, kein Admin nötig.
- Arduino Core 3.3.x über die pioarduino-Plattform
  (`pioarduino/platform-espressif32`), da das offizielle PlatformIO-
  `espressif32` nur Core 2.0.17 liefert, dem die benötigten
  USB-Deskriptor-APIs fehlen. Exakte Versions-URL wird bei der Einrichtung
  festgelegt.
- `boards/dongles3.json` wird aus dem LILYGO-Repo ins Projekt kopiert.
- `platformio.ini`: `board = dongles3`, Build-Flags `ARDUINO_USB_MODE=0`
  (USB-OTG/TinyUSB) und `ARDUINO_USB_CDC_ON_BOOT=0`.

## Flash- und Recovery-Workflow

- Erster Flash: über den vorhandenen COM-Port (Werks-Firmware stellt
  COM3 bereit).
- Nach dem Flash meldet sich das Gerät nur noch als HID-Maus → Reflashen
  nur über Download-Modus: BOOT gedrückt halten, einstecken, loslassen;
  dann erscheint die ROM-Serial/JTAG-Schnittstelle (VID 0x303A) und
  `pio run -t upload` funktioniert wieder.

## Testplan

1. Build läuft fehlerfrei durch.
2. Flash auf das Gerät.
3. Windows-seitige Prüfung per PowerShell/PnP:
   - genau ein HID-Gerät mit getarnter VID/PID und den gesetzten Strings,
   - **kein** COM-Port, **kein** USB-Verbundgerät des Dongles.
4. Manuell durch Benutzer: Cursor bewegt sich gelegentlich minimal,
   driftet nicht weg; Windows-Sperrung/Ruhezustand wird verhindert;
   Taster pausiert/startet; Animation und Statuszeile sichtbar.

## Explizit nicht im Scope (YAGNI)

- Kein WLAN/BLE, keine SD-Karte, kein Web-Interface.
- Keine Absolutmaus, kein Keyboard-HID, keine Mass-Storage-Funktion.
- Kein Konfigurationsmenü; Jiggle-Parameter sind Build-Konstanten.
