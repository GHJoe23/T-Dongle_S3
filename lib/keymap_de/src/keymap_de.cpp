#include "keymap_de.h"

size_t utf8NextCodepoint(const char *utf8, uint32_t &outCodepoint) {
    if (!utf8 || !utf8[0]) {
        outCodepoint = 0;
        return 0;
    }
    const uint8_t b0 = (uint8_t)utf8[0];
    if (b0 < 0x80) {
        outCodepoint = b0;
        return 1;
    }
    if ((b0 & 0xE0) == 0xC0) {
        const uint8_t b1 = (uint8_t)utf8[1];
        if ((b1 & 0xC0) != 0x80) {
            outCodepoint = '?';
            return 1;
        }
        outCodepoint = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
        return 2;
    }
    if ((b0 & 0xF0) == 0xE0) {
        const uint8_t b1 = (uint8_t)utf8[1];
        const uint8_t b2 = (uint8_t)utf8[2];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
            outCodepoint = '?';
            return 1;
        }
        outCodepoint = ((uint32_t)(b0 & 0x0F) << 12) |
                       ((uint32_t)(b1 & 0x3F) << 6) |
                       (uint32_t)(b2 & 0x3F);
        return 3;
    }
    if ((b0 & 0xF8) == 0xF0) {
        const uint8_t b1 = (uint8_t)utf8[1];
        const uint8_t b2 = (uint8_t)utf8[2];
        const uint8_t b3 = (uint8_t)utf8[3];
        if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
            outCodepoint = '?';
            return 1;
        }
        outCodepoint = ((uint32_t)(b0 & 0x07) << 18) |
                       ((uint32_t)(b1 & 0x3F) << 12) |
                       ((uint32_t)(b2 & 0x3F) << 6) |
                       (uint32_t)(b3 & 0x3F);
        return 4;
    }
    outCodepoint = '?';
    return 1;
}

static bool mapDeQwertz(uint32_t cp, HidKeyStroke &out) {
    out.modifiers = HID_MOD_NONE;
    out.keycode = 0;
    out.deadKeySpace = false;

    // Kleinbuchstaben a..z (mit y/z-Tausch auf DE-QWERTZ)
    if (cp >= 'a' && cp <= 'z') {
        if (cp == 'y') out.keycode = 0x1D;      // US 'z' Position
        else if (cp == 'z') out.keycode = 0x1C; // US 'y' Position
        else out.keycode = (uint8_t)(0x04 + (cp - 'a'));
        return true;
    }

    // Großbuchstaben A..Z (mit Y/Z-Tausch auf DE-QWERTZ)
    if (cp >= 'A' && cp <= 'Z') {
        out.modifiers = HID_MOD_LSHIFT;
        if (cp == 'Y') out.keycode = 0x1D;
        else if (cp == 'Z') out.keycode = 0x1C;
        else out.keycode = (uint8_t)(0x04 + (cp - 'A'));
        return true;
    }

    // Ziffern 1..9 und 0
    if (cp >= '1' && cp <= '9') {
        out.keycode = (uint8_t)(0x1E + (cp - '1'));
        return true;
    }
    if (cp == '0') {
        out.keycode = 0x27;
        return true;
    }

    switch (cp) {
        // Steuerzeichen & Whitespace
        case '\n':
        case '\r': out.keycode = 0x28; return true; // Enter
        case '\t': out.keycode = 0x2B; return true; // Tab
        case '\b': out.keycode = 0x2A; return true; // Backspace
        case ' ':  out.keycode = 0x2C; return true; // Space

        // Deutsche Umlaute & Eszett
        case 0x00E4: out.keycode = 0x34; return true;                             // ä
        case 0x00C4: out.keycode = 0x34; out.modifiers = HID_MOD_LSHIFT; return true; // Ä
        case 0x00F6: out.keycode = 0x33; return true;                             // ö
        case 0x00D6: out.keycode = 0x33; out.modifiers = HID_MOD_LSHIFT; return true; // Ö
        case 0x00FC: out.keycode = 0x2F; return true;                             // ü
        case 0x00DC: out.keycode = 0x2F; out.modifiers = HID_MOD_LSHIFT; return true; // Ü
        case 0x00DF: out.keycode = 0x2D; return true;                             // ß
        case 0x1E9E: out.keycode = 0x2D; out.modifiers = HID_MOD_LSHIFT | HID_MOD_RALT; return true; // ẞ

        // Shift + Zahlenreihe (DE)
        case '!':    out.keycode = 0x1E; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+1
        case '"':    out.keycode = 0x1F; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+2
        case 0x00A7: out.keycode = 0x20; out.modifiers = HID_MOD_LSHIFT; return true; // § (Shift+3)
        case '$':    out.keycode = 0x21; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+4
        case '%':    out.keycode = 0x22; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+5
        case '&':    out.keycode = 0x23; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+6
        case '/':    out.keycode = 0x24; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+7
        case '(':    out.keycode = 0x25; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+8
        case ')':    out.keycode = 0x26; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+9
        case '=':    out.keycode = 0x27; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+0
        case '?':    out.keycode = 0x2D; out.modifiers = HID_MOD_LSHIFT; return true; // Shift+ß

        // AltGr (RALT) Kombinationen auf DE-Tastatur
        case 0x00B2: out.keycode = 0x1F; out.modifiers = HID_MOD_RALT; return true; // ² (AltGr+2)
        case 0x00B3: out.keycode = 0x20; out.modifiers = HID_MOD_RALT; return true; // ³ (AltGr+3)
        case '{':    out.keycode = 0x24; out.modifiers = HID_MOD_RALT; return true; // AltGr+7
        case '[':    out.keycode = 0x25; out.modifiers = HID_MOD_RALT; return true; // AltGr+8
        case ']':    out.keycode = 0x26; out.modifiers = HID_MOD_RALT; return true; // AltGr+9
        case '}':    out.keycode = 0x27; out.modifiers = HID_MOD_RALT; return true; // AltGr+0
        case '\\':   out.keycode = 0x2D; out.modifiers = HID_MOD_RALT; return true; // AltGr+ß
        case '@':    out.keycode = 0x14; out.modifiers = HID_MOD_RALT; return true; // AltGr+Q
        case 0x20AC: out.keycode = 0x08; out.modifiers = HID_MOD_RALT; return true; // € (AltGr+E)
        case '~':    out.keycode = 0x30; out.modifiers = HID_MOD_RALT; return true; // AltGr++
        case '|':    out.keycode = 0x64; out.modifiers = HID_MOD_RALT; return true; // AltGr+<
        case 0x00B5: out.keycode = 0x10; out.modifiers = HID_MOD_RALT; return true; // µ (AltGr+M)

        // Weitere Tasten DE-Layout
        case '+':    out.keycode = 0x30; return true;
        case '*':    out.keycode = 0x30; out.modifiers = HID_MOD_LSHIFT; return true;
        case '#':    out.keycode = 0x31; return true;
        case '\'':   out.keycode = 0x31; out.modifiers = HID_MOD_LSHIFT; return true;
        case ',':    out.keycode = 0x36; return true;
        case ';':    out.keycode = 0x36; out.modifiers = HID_MOD_LSHIFT; return true;
        case '.':    out.keycode = 0x37; return true;
        case ':':    out.keycode = 0x37; out.modifiers = HID_MOD_LSHIFT; return true;
        case '-':    out.keycode = 0x38; return true;
        case '_':    out.keycode = 0x38; out.modifiers = HID_MOD_LSHIFT; return true;
        case '<':    out.keycode = 0x64; return true;
        case '>':    out.keycode = 0x64; out.modifiers = HID_MOD_LSHIFT; return true;
        case 0x00B0: out.keycode = 0x35; out.modifiers = HID_MOD_LSHIFT; return true; // ° (Shift+^)

        // Dead Keys auf DE-Tastatur (^, `, ´): benötigen Space danach
        case '^':    out.keycode = 0x35; out.deadKeySpace = true; return true;
        case '`':    out.keycode = 0x2E; out.modifiers = HID_MOD_LSHIFT; out.deadKeySpace = true; return true;
        case 0x00B4: out.keycode = 0x2E; out.deadKeySpace = true; return true; // ´

        default:
            return false;
    }
}

static bool mapUsQwerty(uint32_t cp, HidKeyStroke &out) {
    out.modifiers = HID_MOD_NONE;
    out.keycode = 0;
    out.deadKeySpace = false;

    if (cp >= 'a' && cp <= 'z') {
        out.keycode = (uint8_t)(0x04 + (cp - 'a'));
        return true;
    }
    if (cp >= 'A' && cp <= 'Z') {
        out.modifiers = HID_MOD_LSHIFT;
        out.keycode = (uint8_t)(0x04 + (cp - 'A'));
        return true;
    }
    if (cp >= '1' && cp <= '9') {
        out.keycode = (uint8_t)(0x1E + (cp - '1'));
        return true;
    }
    if (cp == '0') {
        out.keycode = 0x27;
        return true;
    }

    switch (cp) {
        case '\n':
        case '\r': out.keycode = 0x28; return true;
        case '\t': out.keycode = 0x2B; return true;
        case '\b': out.keycode = 0x2A; return true;
        case ' ':  out.keycode = 0x2C; return true;

        case '!':  out.keycode = 0x1E; out.modifiers = HID_MOD_LSHIFT; return true;
        case '@':  out.keycode = 0x1F; out.modifiers = HID_MOD_LSHIFT; return true;
        case '#':  out.keycode = 0x20; out.modifiers = HID_MOD_LSHIFT; return true;
        case '$':  out.keycode = 0x21; out.modifiers = HID_MOD_LSHIFT; return true;
        case '%':  out.keycode = 0x22; out.modifiers = HID_MOD_LSHIFT; return true;
        case '^':  out.keycode = 0x23; out.modifiers = HID_MOD_LSHIFT; return true;
        case '&':  out.keycode = 0x24; out.modifiers = HID_MOD_LSHIFT; return true;
        case '*':  out.keycode = 0x25; out.modifiers = HID_MOD_LSHIFT; return true;
        case '(':  out.keycode = 0x26; out.modifiers = HID_MOD_LSHIFT; return true;
        case ')':  out.keycode = 0x27; out.modifiers = HID_MOD_LSHIFT; return true;

        case '-':  out.keycode = 0x2D; return true;
        case '_':  out.keycode = 0x2D; out.modifiers = HID_MOD_LSHIFT; return true;
        case '=':  out.keycode = 0x2E; return true;
        case '+':  out.keycode = 0x2E; out.modifiers = HID_MOD_LSHIFT; return true;
        case '[':  out.keycode = 0x2F; return true;
        case '{':  out.keycode = 0x2F; out.modifiers = HID_MOD_LSHIFT; return true;
        case ']':  out.keycode = 0x30; return true;
        case '}':  out.keycode = 0x30; out.modifiers = HID_MOD_LSHIFT; return true;
        case '\\': out.keycode = 0x31; return true;
        case '|':  out.keycode = 0x31; out.modifiers = HID_MOD_LSHIFT; return true;
        case ';':  out.keycode = 0x33; return true;
        case ':':  out.keycode = 0x33; out.modifiers = HID_MOD_LSHIFT; return true;
        case '\'': out.keycode = 0x34; return true;
        case '"':  out.keycode = 0x34; out.modifiers = HID_MOD_LSHIFT; return true;
        case '`':  out.keycode = 0x35; return true;
        case '~':  out.keycode = 0x35; out.modifiers = HID_MOD_LSHIFT; return true;
        case ',':  out.keycode = 0x36; return true;
        case '<':  out.keycode = 0x36; out.modifiers = HID_MOD_LSHIFT; return true;
        case '.':  out.keycode = 0x37; return true;
        case '>':  out.keycode = 0x37; out.modifiers = HID_MOD_LSHIFT; return true;
        case '/':  out.keycode = 0x38; return true;
        case '?':  out.keycode = 0x38; out.modifiers = HID_MOD_LSHIFT; return true;
        default:
            return false;
    }
}

bool mapCodepointToHid(uint32_t codepoint, KeyboardLayout layout, HidKeyStroke &outStroke) {
    if (layout == KeyboardLayout::US_QWERTY) {
        return mapUsQwerty(codepoint, outStroke);
    }
    return mapDeQwertz(codepoint, outStroke);
}
