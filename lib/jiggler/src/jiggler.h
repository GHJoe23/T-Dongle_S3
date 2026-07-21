#pragma once
#include <stdint.h>

class Jiggler {
public:
    void begin(uint32_t now, uint32_t seed);
    void toggle(uint32_t now);
    bool active() const;
    bool poll(uint32_t now, int8_t &dx, int8_t &dy);
    uint32_t msUntilNext(uint32_t now) const;
    uint32_t count() const;

private:
    uint32_t _next = 0;
    uint32_t _rng = 1;
    uint32_t _count = 0;
    bool _active = false;
    bool _pendingReturn = false;
    int8_t _lastDx = 0;
    int8_t _lastDy = 0;

    uint32_t _rand();
    uint32_t _interval();
};
