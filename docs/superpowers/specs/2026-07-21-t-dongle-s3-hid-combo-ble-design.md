# Design: HID-Kombo (Tastatur+Maus) + BLE-Smartphone-Fernbedienung

Datum: 2026-07-21
Status: freigegeben (Design-Phase abgeschlossen)
Vorgänger: `2026-07-21-t-dongle-s3-mouse-jiggler-design.md` (Jiggler bleibt erhalten)

## Ziel

Der T-Dongle-S3 meldet sich nicht mehr nur als Maus, sondern als
USB-HID-Kombigerät (Tastatur + Maus) im Stil eines Logitech-Unifying-
Empfängers (K400-Serie). Zusätzlich verbindet sich ein Smartphone per
Bluetooth (BLE) mit dem Dongle und kann am PC Text tippen, Tastenkürzel
auslösen und vordefinierte Makros starten. Der bestehende Mouse-Jiggler
bleibt unverändert aktiv.

## Machbarkeit (vorab geprüft)

- Mehrere HID-Interfaces (Tastatur + Maus) in einem Verbundgerät: vom
  TinyUSB-Stack des Arduino Core 3.3 unterstützt (`USBHIDKeyboard` +
  `USBHIDMouse` vor `USB.begin()`).
- BLE parallel zu USB: unabhängige Peripherieblöcke am ESP32-S3.
- Kein PSRAM auf dem Board → NimBLE-Stack statt Bluedroid.
- Keine eigene Handy-App nötig: generisches BLE-Terminal genügt.

## Architektur

Ansatz A (freigegeben): Composite-HID + NimBLE-GATT-UART.

### USB-Identität

- Verbundgerät mit zwei HID-Interfaces: Tastatur + Maus
- VID `0x046D`, PID `0xC52B`, Manufacturer `Logitech`, Product
  `USB Receiver` (Erscheinungsbild eines Unifying-Empfängers)
- Weiterhin: kein COM-Port, kein MSC, kein JTAG

### BLE-Fernbedienung

- NimBLE-Arduino (Library `h2zero/NimBLE-Arduino`)
- GATT-Service im Nordic-UART-Stil
  (`6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, RX `6E400002-…`, TX `6E400003-…`)
- Sicherheit: Bonding mit Passkey — das Display zeigt beim Pairing eine
  zufällige 6-stellige PIN, die am Handy eingegeben wird. Nur gekoppelte
  Geräte dürfen auf die RX-Charakteristik schreiben.

### Kommandoprotokoll (zeilenbasiert, `\n`-terminiert)

| Kommando | Wirkung |
|---|---|
| `T <text>` | Text als Tastatureingabe (v1: ASCII/US-Layout) |
| `K <MOD+TASTE>` | Tastenkürzel, z.B. `K STRG+ALT+ENTF`, `K GUI+L`, `K ALT+F4` |
| `M<n>` | Makro n starten, n = 1..9 |
| `J` | Jiggler AN/PAUSE umschalten |

Antworten über TX: `OK` oder `ERR <grund>`. Unbekannte Kommandos →
`ERR` + kurze Anzeige auf dem Display.

### Makros

- Fest in der Firmware: Tabelle von Schritten `(Wartezeit ms, Text oder
  Kürzel)`; Beispiele: Bildschirm sperren (`GUI+L`), Task-Manager
  (`STRG+UMSCHALT+ESC`)
- Keine Passwörter oder Geheimnisse in den Beispiel-Makros
- Abarbeitung nicht-blockierend über Zeitschritte; während ein Makro
  läuft, werden weitere Makro-Kommandos mit `ERR busy` abgelehnt

### Module

| Modul | Verantwortung |
|---|---|
| `usb_hid` | Composite-Setup (Tastatur+Maus), `typeText`, `pressCombo` |
| `ble_remote` | NimBLE-Service, Passkey-Pairing, Empfang → Kommandozeilen |
| `macro_engine` | Parser (Kommando → Aktion), Makrotabelle, nicht-blockierende Abarbeitung; host-testbar, keine Arduino-Abhängigkeiten |
| `jiggler` | unverändert (besteht, getestet) |
| `display_ui` | Animation + Statuszeile, neu: BLE-Zustand, Pairing-PIN, letztes Kommando |
| `main.cpp` | Verdrahtung |

### Datenfluss

BLE-RX → `ble_remote` puffert Zeile → `macro_engine.parse()` → Aktion
(`typeText`/`pressCombo`/`Makro starten`/Jiggler-Toggle) → Antwort via
BLE-TX. `macro_engine.tick(now)` führt Makroschritte zeitgesteuert aus.
Display zeigt BLE-Zustand, PIN (während Pairing), Status und kurz das
letzte Kommando.

### Fehlerbehandlung

- Unbekanntes/fehlerhaftes Kommando: `ERR <grund>` via TX + Display-Hinweis
- Makro während laufendem Makro: `ERR busy`
- BLE-Verbindungsabbruch: Statusanzeige `BLE wartet`, Jiggler läuft weiter
- Bonding entfernt → neues Pairing mit neuer PIN jederzeit möglich

## Tests

1. Native Unit-Tests: Parser (alle Kommandos, Fehlerfälle), Makro-
   Abarbeitung (Schrittfolge, Timing, busy-Ablehnung) — mit injizierter Zeit
2. Target-Build muss fehlerfrei durchlaufen
3. Windows-PnP: genau ein Verbundgerät VID `046D`/PID `C52B` mit
   Tastatur- und Maus-HID, kein COM-Port
4. Handy-Test: Pairing mit Display-PIN, dann `T`, `K`, `M1`, `J`
   durchspielen (z.B. im Editor sichtbar)

## Explizit nicht im Scope (YAGNI)

- Keine Handy-App-Entwicklung (generisches BLE-Terminal genügt)
- Keine Maussteuerung vom Handy (Maus-Interface bleibt dem Jiggler)
- Keine deutschen Umlaute/Layout-Umstellung (v1: ASCII/US-Layout)
- Keine Makro-Konfiguration zur Laufzeit (SD-Karte, Web-UI)
- Keine WLAN-Nutzung
