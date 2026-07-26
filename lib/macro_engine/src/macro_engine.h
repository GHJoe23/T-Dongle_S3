#pragma once
#include <stdint.h>

enum class ActionType : uint8_t { TypeText, Combo, ToggleJiggler, Error };

struct Action {
    ActionType type;
    char text[64];
    uint8_t mods[4];
    uint8_t modCount;
    uint8_t key;
};

struct MacroStep {
    uint32_t delayMs;
    Action action;
};

struct Macro {
    const MacroStep *steps;
    uint8_t count;
};

class MacroEngine {
public:
    typedef void (*EmitFn)(const Action &a, void *ctx);

    MacroEngine(const Macro *macros, uint8_t macroCount);
    void begin(EmitFn emit, void *ctx);
    bool handleLine(uint32_t now, const char *line);
    bool busy() const { return _macroIdx >= 0; }
    void tick(uint32_t now);

private:
    const Macro *_macros;
    uint8_t _macroCount;
    EmitFn _emit = nullptr;
    void *_ctx = nullptr;
    int8_t _macroIdx = -1;
    uint8_t _step = 0;
    uint32_t _nextAt = 0;

    bool parseCombo(const char *s, Action &out);
    void emitError(const char *reason);
    void startMacro(uint32_t now, uint8_t idx);
};
