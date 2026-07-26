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
    // Format: MOD+MOD+TASTE (letztes Token = Taste)
    memset(&out, 0, sizeof(out));
    out.type = ActionType::Combo;
    char buf[48];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    char *tokens[8];
    int n = 0;
    char *p = strtok(buf, "+");
    while (p && n < 8) { tokens[n++] = p; p = strtok(nullptr, "+"); }
    if (n == 0) return false;

    for (int i = 0; i < n - 1; i++) {
        const char *t = tokens[i];
        uint8_t code;
        if (strcmp(t, "STRG") == 0) code = 0x80;
        else if (strcmp(t, "UMSCHALT") == 0) code = 0x81;
        else if (strcmp(t, "ALT") == 0) code = 0x82;
        else if (strcmp(t, "GUI") == 0) code = 0x83;
        else return false;
        if (out.modCount >= 4) return false;
        out.mods[out.modCount++] = code;
    }

    const char *k = tokens[n - 1];
    int len = strlen(k);
    if (len == 1) {
        out.key = (uint8_t)toupper((unsigned char)k[0]);
    } else if (tokenEq(k, len, "ENTF")) out.key = 0xD4;
    else if (tokenEq(k, len, "ESC")) out.key = 0xB1;
    else if (tokenEq(k, len, "ENTER")) out.key = 0xB0;
    else if (tokenEq(k, len, "TAB")) out.key = 0xB3;
    else if (tokenEq(k, len, "LEER")) out.key = ' ';
    else if (k[0] == 'F' && len >= 2) {
        int f = atoi(k + 1);
        if (f < 1 || f > 12) return false;
        out.key = 0xC1 + (uint8_t)f; // F1=0xC2 .. F12=0xCD
    } else return false;
    return true;
}

bool MacroEngine::handleLine(uint32_t now, const char *line) {
    if (line[0] == 'J' && line[1] == 0) {
        Action a{};
        a.type = ActionType::ToggleJiggler;
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (line[0] == 'T' && line[1] == ' ') {
        Action a{};
        a.type = ActionType::TypeText;
        strncpy(a.text, line + 2, sizeof(a.text) - 1);
        if (_emit) _emit(a, _ctx);
        return true;
    }
    if (line[0] == 'K' && line[1] == ' ') {
        Action a;
        if (!parseCombo(line + 2, a)) {
            emitError("bad combo");
            return false;
        }
        if (_emit) _emit(a, _ctx);
        return true;
    }
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
