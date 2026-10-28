# LILYGO T-Dongle-S3 • Logitech K400+ HID-Combo & Pixel 10 Pro XL WebBluetooth Suite

Getarnter USB-HID-Combo-Dongle (**Tastatur + Maus + Media-Keys + Mouse-Jiggler**) für den **LILYGO T-Dongle-S3 (ESP32-S3)** mit passender **WebBluetooth Progressive Web App (PWA)** für das **Google Pixel 10 Pro XL**.

## Features

- **Perfekte Tarnung (Logitech K400 Plus / Unifying):**
  - Meldet sich am PC als `Logitech` / `USB Receiver` (`VID 0x046D`, `PID 0xC52B`) mit leerer Seriennummer und **ohne COM-Port**.
  - 3-fach Composite-HID: **Tastatur** (`USBHIDKeyboard`), **Maus** (`USBHIDMouse`) und **Media-Steuerung** (`USBHIDConsumerControl`).
- **Echtes Deutsches Tastaturlayout (DE-QWERTZ):**
  - Vollständiger UTF-8-zu-HID-Scancode-Mapper für `DE-QWERTZ` (inkl. `y`/`z`-Zuordnung, Umlaute `äöüÄÖÜß`, `AltGr`-Kombinationen wie `@`, `\`, `|`, `{`, `[`, `]`, `}`, `~`, `€`, `µ` sowie Dead-Key-Auflösung für `^` und `` ` ``).
  - Zur Laufzeit umschaltbar zwischen `DE-QWERTZ` und `US-QWERTY`.
- **Konfigurierbarer Mouse-Jiggler (mit NVS-Speicherung):**
  - 3 Modi:
    - `0` **Ping-Pong**: ±1..4 px, nächster Tick bewegt exakt zurück.
    - `1` **Instant-Return (0 px Drift)**: Bewegt ±1..4 px und kehrt nach 35 ms sofort auf den Ausgangspunkt zurück.
    - `2` **Natural Arc**: Sanfte geschlossene 4-Schritt-Mikrokurve.
  - Frei einstellbares Zufallsintervall (`minSec`..`maxSec`, Standard 30–60 s) & Auto-Start.
- **Pixel 10 Pro XL WebBluetooth PWA (`docs/`):**
  - Direkt im Chrome-Browser auf dem Pixel 10 Pro XL über **Web Bluetooth** nutzbar und als **Offline-PWA** installierbar (gehostet via GitHub Pages).
  - **Feste BLE-PIN:** Standardmäßig **`400400`** (im NVS speicherbar & per App änderbar) – einmal koppeln, danach sofortiges Wiederverbinden ohne erneute PIN-Eingabe.
  - **Textmodule / Snippets:** Beliebig lange, mehrzeilige Textbausteine auf dem Smartphone verwalten (mit Kategorien & JSON-Backup) oder direkt in den **12 Hardware-Slots (NVS)** auf dem Dongle speichern.
  - **Multi-Touch-Trackpad & Media-Deck:** Flüssige Maussteuerung, Scroll-Leiste, Links-/Rechts-/Doppelklick, Drag-Modus, Media-Tasten und Windows/Linux-Shortcuts (`Win+L`, `Task-Manager`, `Strg+C/V/Z` etc.).
- **Stealth-Modus & Software-Bootloader:**
  - **Stealth-Modus:** Schaltet Display-Backlight und APA102-LED komplett dunkel.
  - **Software-Bootloader (`SYS BOOTLOADER`):** Versetzt den Dongle per Web-App (oder durch 5 s Halten des Hardware-Tasters) direkt in den ESP32-S3 Flash-/Download-Modus – kein Fummeln am Taster beim Einstecken mehr nötig!

---

## Hardware-Taster (GPIO 0) am Dongle

| Druckdauer | Funktion |
|---|---|
| **Kurz (< 1 s)** | Mouse-Jiggler **AN / PAUSE** umschalten |
| **Mittel (1 – 4,9 s)** | **Stealth-Modus AN / AUS** (Display & LED komplett dunkel/hell) |
| **Lang (≥ 5 s halten)** | **Sofort in den USB-Bootloader / Flash-Modus starten** (Blauer Bildschirm) |

---

## BLE-Kommandoprotokoll (Nordic UART Service)

- **BLE-Name:** `USB Receiver`
- **Service UUID:** `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **RX (Write / Write Without Response):** `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- **TX (Notify):** `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- **Standard-PIN:** `400400`

| Kommando | Beschreibung |
|---|---|
| `T <text>` | Tippt UTF-8-Text im aktiven Layout (`\n` = Enter, `\t` = Tab) |
| `K <MOD+TASTE>` | Tastenkürzel, z.B. `K GUI+L`, `K STRG+C`, `K STRG+Z`, `K ALT+TAB` |
| `M <dx> <dy> [wheel]` | Relative Mausbewegung & Mausrad |
| `C <L\|R\|M\|DBL\|DOWN\|UP>` | Mausklick / Drag |
| `MEDIA <PLAY\|MUTE\|VOL+\|VOL-\|NEXT\|PREV>` | Multimedia-Tasten |
| `J [ON\|OFF\|TOGGLE]` | Jiggler ein-/ausschalten oder umschalten |
| `JCFG <minS> <maxS> <mode> <auto>` | Jiggler konfigurieren & im NVS speichern |
| `SNIP SET <id>\|<titel>\|<text>` | Textmodul in Dongle-NVS (Slot `0..11`) speichern |
| `SNIP RUN <id>` | Gespeichertes Dongle-Textmodul tippen |
| `SNIP DEL <id>` / `SNIP LIST` | Dongle-Slot löschen / alle Slots auflisten |
| `SYS STATUS` | Aktuellen Gerätestatus als `STAT {...}` JSON abrufen |
| `SYS LAYOUT <DE\|US>` | Tastaturlayout wechseln (`DE-QWERTZ` / `US-QWERTY`) |
| `SYS STEALTH <0\|1>` | Stealth-Modus (Display & LED aus) setzen |
| `SYS DELAY <ms>` | Tipp-Verzögerung (ms) anpassen |
| `SYS PIN <6-stellig>` | Feste BLE-PIN ändern & im NVS speichern |
| `SYS BOOTLOADER` | Dongle sofort in den USB-Flash-Modus neustarten |

---

## Build, Test & Flash

```powershell
# 1. Native Unit-Tests (Keymap DE, Jiggler, Macro Engine)
$env:PATH = "$PWD\tools\shims;$env:PATH"; $env:MSYSTEM = ""; .\.venv\Scripts\pio.exe test -e native

# 2. Firmware bauen
cmd /c "tools\pio-win.bat run -e T-Dongle-S3"

# 3. Firmware flashen (Dongle im Bootloader-Modus an COMx)
cmd /c "tools\pio-win.bat run -e T-Dongle-S3 -t upload --upload-port COMx"
```
