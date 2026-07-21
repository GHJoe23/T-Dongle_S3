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
