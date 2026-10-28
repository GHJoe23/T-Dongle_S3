#include "macro_engine.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

MacroEngine::MacroEngine(const Macro *macros, uint8_t macroCount)
    : _macros(macros), _macroCount(macroCount) {}

void MacroEngine::begin(EmitFn emit, void *ctx) {
    _emit = emit;
    _ctx = ctx;
    _macroIdx = -1;
    _step = 0;
}

void MacroEngine::emitError(const char *reason) {
    Action a{};
    a.type = ActionType::Error;
    strncpy(a.text, reason, sizeof(a.text) - 1);
    if (_emit) _emit(a, _ctx);
}

static bool tokenEq(const char *tok, int len, const char *name) {
    return (int)strlen(name) == len && strncmp(tok, name, len) == 0;
}

bool MacroEngine::parseCombo(const char *s, Action &out) {
    // Format: [MOD+]*TASTE
    memset(&out, 0, sizeof(out));
    out.type = ActionType::Combo;
    char buf[64];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *tokens[8];
    int n = 0;
    char *p = strtok(buf, "+");
    while (p && n < 8) {
        tokens[n++] = p;
        p = strtok(nullptr, "+");
    }
    if (n == 0) return false;

    for (int i = 0; i < n - 1; i++) {
        const char *t = tokens[i];
        uint8_t code;
        if (strcmp(t, "STRG") == 0 || strcmp(t, "CTRL") == 0) code = 0x80;
        else if (strcmp(t, "UMSCHALT") == 0 || strcmp(t, "SHIFT") == 0) code = 0x81;
        else if (strcmp(t, "ALT") == 0) code = 0x82;
        else if (strcmp(t, "GUI") == 0 || strcmp(t, "WIN") == 0 || strcmp(t, "META") == 0) code = 0x83;
        else if (strcmp(t, "ALTGR") == 0 || strcmp(t, "RALT") == 0) code = 0x86;
        else return false;
        if (out.modCount >= 4) return false;
        out.mods[out.modCount++] = code;
    }

    const char *k = tokens[n - 1];
    int len = (int)strlen(k);
    if (len == 1) {
        out.key = (uint8_t)toupper((unsigned char)k[0]);
    } else if (tokenEq(k, len, "ENTF") || tokenEq(k, len, "DEL")) out.key = 0xD4;
    else if (tokenEq(k, len, "ESC")) out.key = 0xB1;
    else if (tokenEq(k, len, "ENTER") || tokenEq(k, len, "RETURN")) out.key = 0xB0;
    else if (tokenEq(k, len, "TAB")) out.key = 0xB3;
    else if (tokenEq(k, len, "LEER") || tokenEq(k, len, "SPACE")) out.key = ' ';
    else if (tokenEq(k, len, "BACKSPACE") || tokenEq(k, len, "BS")) out.key = 0xB2;
    else if (tokenEq(k, len, "UP") || tokenEq(k, len, "HOCH")) out.key = 0xDA;
    else if (tokenEq(k, len, "DOWN") || tokenEq(k, len, "RUNTER")) out.key = 0xD9;
    else if (tokenEq(k, len, "LEFT") || tokenEq(k, len, "LINKS")) out.key = 0xD8;
    else if (tokenEq(k, len, "RIGHT") || tokenEq(k, len, "RECHTS")) out.key = 0xD7;
    else if (tokenEq(k, len, "EINFG") || tokenEq(k, len, "INS")) out.key = 0xD1;
    else if (tokenEq(k, len, "POS1") || tokenEq(k, len, "HOME")) out.key = 0xD2;
    else if (tokenEq(k, len, "ENDE") || tokenEq(k, len, "END")) out.key = 0xD5;
    else if (tokenEq(k, len, "BILDUP") || tokenEq(k, len, "PGUP")) out.key = 0xD3;
    else if (tokenEq(k, len, "BILDDOWN") || tokenEq(k, len, "PGDN")) out.key = 0xD6;
    else if (tokenEq(k, len, "DRUCK") || tokenEq(k, len, "PRTSC")) out.key = 0xCE;
    else if (tokenEq(k, len, "WIN") || tokenEq(k, len, "GUI")) {
        if (out.modCount >= 4) return false;
        out.mods[out.modCount++] = 0x83;
        out.key = 0;
    } else if (k[0] == 'F' && len >= 2) {
        int f = atoi(k + 1);
        if (f < 1 || f > 12) return false;
        out.key = 0xC1 + (uint8_t)f; // F1=0xC2 .. F12=0xCD
    } else return false;
    return true;
}

bool MacroEngine::parseMouseMove(const char *s, Action &out) {
    memset(&out, 0, sizeof(out));
    out.type = ActionType::MouseMove;
    char *end1 = nullptr;
    long dx = strtol(s, &end1, 10);
    if (end1 == s || *end1 != ' ') return false;
    char *end2 = nullptr;
    long dy = strtol(end1 + 1, &end2, 10);
    if (end2 == end1 + 1) return false;
    long wheel = 0;
    long pan = 0;
    if (*end2 == ' ') {
        char *end3 = nullptr;
        wheel = strtol(end2 + 1, &end3, 10);
        if (end3 && *end3 == ' ') {
            pan = strtol(end3 + 1, nullptr, 10);
        }
    }
    if (dx < -127) dx = -127; else if (dx > 127) dx = 127;
    if (dy < -127) dy = -127; else if (dy > 127) dy = 127;
    if (wheel < -127) wheel = -127; else if (wheel > 127) wheel = 127;
    if (pan < -127) pan = -127; else if (pan > 127) pan = 127;
    out.intParams[0] = (int32_t)dx;
    out.intParams[1] = (int32_t)dy;
    out.intParams[2] = (int32_t)wheel;
    out.intParams[3] = (int32_t)pan;
    return true;
}

bool MacroEngine::parseMouseClick(const char *s, Action &out) {
    memset(&out, 0, sizeof(out));
    out.type = ActionType::MouseClick;
    if (strcmp(s, "L") == 0) out.key = 'L';
    else if (strcmp(s, "R") == 0) out.key = 'R';
    else if (strcmp(s, "M") == 0) out.key = 'M';
    else if (strcmp(s, "DBL") == 0) out.key = 'D';
    else if (strcmp(s, "DOWN") == 0) out.key = 'd';
    else if (strcmp(s, "UP") == 0) out.key = 'u';
    else if (strcmp(s, "RDOWN") == 0) out.key = 'r';
    else if (strcmp(s, "RUP") == 0) out.key = 's';
    else return false;
    return true;
}

bool MacroEngine::parseMedia(const char *s, Action &out) {
    memset(&out, 0, sizeof(out));
    out.type = ActionType::MediaKey;
    if (strcmp(s, "PLAY") == 0 || strcmp(s, "PAUSE") == 0) out.intParams[0] = 0x00CD;
    else if (strcmp(s, "MUTE") == 0) out.intParams[0] = 0x00E2;
    else if (strcmp(s, "VOL+") == 0 || strcmp(s, "VOLUP") == 0) out.intParams[0] = 0x00E9;
    else if (strcmp(s, "VOL-") == 0 || strcmp(s, "VOLDOWN") == 0) out.intParams[0] = 0x00EA;
    else if (strcmp(s, "NEXT") == 0) out.intParams[0] = 0x00B5;
    else if (strcmp(s, "PREV") == 0) out.intParams[0] = 0x00B6;
    else if (strcmp(s, "STOP") == 0) out.intParams[0] = 0x00B7;
    else return false;
    return true;
}

bool MacroEngine::parseJigglerCfg(const char *s, Action &out) {
    memset(&out, 0, sizeof(out));
    out.type = ActionType::ConfigJiggler;
    char buf[64];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *p1 = strtok(buf, " ");
    char *p2 = strtok(nullptr, " ");
    char *p3 = strtok(nullptr, " ");
    char *p4 = strtok(nullptr, " ");
    if (!p1 || !p2 || !p3) return false;

    int minS = atoi(p1);
    int maxS = atoi(p2);
    int mode = atoi(p3);
    int autoStart = p4 ? atoi(p4) : 1;
    if (minS < 2 || maxS < minS || mode < 0 || mode > 2) return false;

    out.intParams[0] = minS;
    out.intParams[1] = maxS;
    out.intParams[2] = mode;
    out.intParams[3] = autoStart ? 1 : 0;
    return true;
}

bool MacroEngine::parseSnippetOp(const char *s, Action &out) {
    memset(&out, 0, sizeof(out));
    out.type = ActionType::SnippetOp;
    if (strcmp(s, "LIST") == 0) {
        out.key = 'L';
        return true;
    }
    if (strncmp(s, "RUN ", 4) == 0) {
        int slot = atoi(s + 4);
        if (slot < 0 || slot >= 16) return false;
        out.key = 'R';
        out.intParams[0] = slot;
        return true;
    }
    if (strncmp(s, "DEL ", 4) == 0) {
        int slot = atoi(s + 4);
        if (slot < 0 || slot >= 16) return false;
        out.key = 'D';
        out.intParams[0] = slot;
        return true;
    }
    if (strncmp(s, "SET ", 4) == 0) {
        // Format: SET <slot>|<title>|<text>
        const char *pSlot = s + 4;
        const char *bar1 = strchr(pSlot, '|');
        if (!bar1) return false;
        int slot = atoi(pSlot);
        if (slot < 0 || slot >= 16) return false;
        const char *bar2 = strchr(bar1 + 1, '|');
        if (!bar2) return false;
        size_t titleLen = (size_t)(bar2 - (bar1 + 1));
        if (titleLen >= sizeof(out.text)) titleLen = sizeof(out.text) - 1;
        memcpy(out.text, bar1 + 1, titleLen);
        out.text[titleLen] = 0;
        out.longText = bar2 + 1;
        out.key = 'S';
        out.intParams[0] = slot;
        return true;
    }
    return false;
}

bool MacroEngine::parseSystemCmd(const char *s, Action &out) {
    memset(&out, 0, sizeof(out));
    out.type = ActionType::SystemCmd;
    if (strcmp(s, "STATUS") == 0) {
        out.key = 'S';
        return true;
    }
    if (strcmp(s, "BOOTLOADER") == 0) {
        out.key = 'B';
        return true;
    }
    if (strncmp(s, "STEALTH ", 8) == 0) {
        out.key = 'H';
        out.intParams[0] = atoi(s + 8) ? 1 : 0;
        return true;
    }
    if (strncmp(s, "LAYOUT ", 7) == 0) {
        out.key = 'L';
        if (strcmp(s + 7, "DE") == 0) out.intParams[0] = 0;
        else if (strcmp(s + 7, "US") == 0) out.intParams[0] = 1;
        else return false;
        return true;
    }
    if (strncmp(s, "DELAY ", 6) == 0) {
        char buf[48];
        strncpy(buf, s + 6, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char *p1 = strtok(buf, " ");
        char *p2 = strtok(nullptr, " ");
        char *p3 = strtok(nullptr, " ");
        if (!p1) return false;
        int d = atoi(p1);
        if (d < 1 || d > 500) return false;
        int rnd = p2 ? atoi(p2) : 0;
        int dmax = p3 ? atoi(p3) : (d + 35);
        if (dmax < d) dmax = d;
        if (dmax > 1000) dmax = 1000;
        out.key = 'D';
        out.intParams[0] = d;
        out.intParams[1] = rnd ? 1 : 0;
        out.intParams[2] = dmax;
        return true;
    }
    if (strncmp(s, "WAIT ", 5) == 0) {
        int ms = atoi(s + 5);
        if (ms < 1 || ms > 30000) return false;
        out.key = 'W';
        out.intParams[0] = ms;
        return true;
    }
    if (strncmp(s, "PIN ", 4) == 0) {
        long pin = atol(s + 4);
        if (pin < 100000L || pin > 999999L) return false;
        out.key = 'P';
        out.intParams[0] = (int32_t)pin;
        return true;
    }
    return false;
}

bool MacroEngine::parseMicCmd(const char *s, Action &out) {
    memset(&out, 0, sizeof(out));
    out.type = ActionType::ConfigMic;
    if (strcmp(s, "TOGGLE") == 0) {
        out.key = 'T';
        return true;
    }
    if (strcmp(s, "ON") == 0) {
        out.key = 'M';
        out.intParams[0] = 1;
        return true;
    }
    if (strcmp(s, "OFF") == 0) {
        out.key = 'M';
        out.intParams[0] = 0;
        return true;
    }
    if (strncmp(s, "CFG ", 4) == 0) {
        char buf[48];
        strncpy(buf, s + 4, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = 0;
        char *p1 = strtok(buf, " ");
        char *p2 = strtok(nullptr, " ");
        char *p3 = strtok(nullptr, " ");
        if (!p1 || !p2) return false;
        int mode = atoi(p1);
        int thr = atoi(p2);
        int bossAct = p3 ? atoi(p3) : 0;
        if (mode < 0 || mode > 3 || thr < 5 || thr > 98 || bossAct < 0 || bossAct > 2) {
            return false;
        }
        out.key = 'C';
        out.intParams[0] = mode;
        out.intParams[1] = thr;
        out.intParams[2] = bossAct;
        return true;
    }
    return false;
}

bool MacroEngine::handleLine(uint32_t now, const char *line) {
    if (!line || !line[0]) {
        emitError("empty");
        return false;
    }

    // J / J ON / J OFF / J TOGGLE
    if (strcmp(line, "J") == 0 || strcmp(line, "J TOGGLE") == 0) {
        Action a{};
        a.type = ActionType::ToggleJiggler;
        a.intParams[0] = -1;
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (strcmp(line, "J ON") == 0) {
        Action a{};
        a.type = ActionType::ToggleJiggler;
        a.intParams[0] = 1;
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (strcmp(line, "J OFF") == 0) {
        Action a{};
        a.type = ActionType::ToggleJiggler;
        a.intParams[0] = 0;
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // JCFG <minS> <maxS> <mode> [autoStart]
    if (strncmp(line, "JCFG ", 5) == 0) {
        Action a;
        if (!parseJigglerCfg(line + 5, a)) {
            emitError("bad jcfg");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // T <text> (beliebig lang via longText-Zeiger, plus Fallback in a.text)
    if (line[0] == 'T' && line[1] == ' ') {
        Action a{};
        a.type = ActionType::TypeText;
        strncpy(a.text, line + 2, sizeof(a.text) - 1);
        a.longText = line + 2;
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // K <MOD+TASTE>
    if (line[0] == 'K' && line[1] == ' ') {
        Action a;
        if (!parseCombo(line + 2, a)) {
            emitError("bad combo");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // M <dx> <dy> [wheel] [pan]
    if (line[0] == 'M' && line[1] == ' ') {
        Action a;
        if (!parseMouseMove(line + 2, a)) {
            emitError("bad move");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // C <L|R|M|DBL|DOWN|UP>
    if (line[0] == 'C' && line[1] == ' ') {
        Action a;
        if (!parseMouseClick(line + 2, a)) {
            emitError("bad click");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // MEDIA <PLAY|MUTE|VOL+|VOL-|NEXT|PREV>
    if (strncmp(line, "MEDIA ", 6) == 0) {
        Action a;
        if (!parseMedia(line + 6, a)) {
            emitError("bad media");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // SNIP <SET|RUN|DEL|LIST>
    if (strncmp(line, "SNIP ", 5) == 0) {
        Action a;
        if (!parseSnippetOp(line + 5, a)) {
            emitError("bad snip");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // SYS <STATUS|STEALTH|LAYOUT|DELAY|PIN|BOOTLOADER> oder direkt BOOTLOADER
    if (strcmp(line, "BOOTLOADER") == 0) {
        Action a{};
        a.type = ActionType::SystemCmd;
        a.key = 'B';
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (strncmp(line, "SYS ", 4) == 0) {
        Action a;
        if (!parseSystemCmd(line + 4, a)) {
            emitError("bad sys");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // MIC / MIC ON / MIC OFF / MIC TOGGLE / MIC CFG <mode> <threshold> [bossAction]
    if (strcmp(line, "MIC") == 0) {
        Action a{};
        a.type = ActionType::ConfigMic;
        a.key = 'T';
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (strncmp(line, "MIC ", 4) == 0) {
        Action a;
        if (!parseMicCmd(line + 4, a)) {
            emitError("bad mic");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }

    // M1..M9 (klassische Makros)
    if (line[0] == 'M' && line[1] >= '1' && line[1] <= '9' && line[2] == 0) {
        uint8_t idx = (uint8_t)(line[1] - '1');
        if (idx >= _macroCount) {
            emitError("unknown macro");
            return false;
        }
        if (busy()) {
            emitError("busy");
            return false;
        }
        startMacro(now, idx);
        return true;
    }

    emitError("unknown command");
    return false;
}

void MacroEngine::startMacro(uint32_t now, uint8_t idx) {
    _macroIdx = (int8_t)idx;
    _step = 0;
    _nextAt = now + _macros[idx].steps[0].delayMs;
}

void MacroEngine::tick(uint32_t now) {
    if (_macroIdx < 0 || (int32_t)(now - _nextAt) < 0) return;
    const Macro *m = &_macros[_macroIdx];
    if (_emit) _emit(m->steps[_step].action, _ctx);
    _step++;
    if (_step >= m->count) {
        _macroIdx = -1;
    } else {
        _nextAt = now + m->steps[_step].delayMs;
    }
}
