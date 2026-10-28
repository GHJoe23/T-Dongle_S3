#pragma once
#include <stdint.h>

enum class JiggleMode : uint8_t {
    PING_PONG = 0,      // Bewegt ±1..4 px, nächstes Intervall bewegt exakt zurück
    INSTANT_RETURN = 1, // Bewegt ±1..4 px und kehrt nach 35 ms sofort zurück
    NATURAL_ARC = 2     // 4 Mikro-Schritte im Abstand von 25 ms (geschlossene Kurve)
};

class Jiggler {
public:
    void begin(uint32_t now, uint32_t seed);
    void toggle(uint32_t now);
    void setActive(uint32_t now, bool enable);
    void configure(uint32_t now, uint16_t minSec, uint16_t maxSec, JiggleMode mode);

    bool active() const { return _active; }
    uint16_t minSec() const { return _minSec; }
    uint16_t maxSec() const { return _maxSec; }
    JiggleMode mode() const { return _mode; }

    bool poll(uint32_t now, int8_t &dx, int8_t &dy);
    uint32_t msUntilNext(uint32_t now) const;
    uint32_t count() const { return _count; }

private:
    uint32_t _next = 0;
    uint32_t _rng = 1;
    uint32_t _count = 0;
    uint16_t _minSec = 30;
    uint16_t _maxSec = 60;
    JiggleMode _mode = JiggleMode::PING_PONG;
    bool _active = false;
    uint8_t _subStep = 0;
    int8_t _lastDx = 0;
    int8_t _lastDy = 0;

    uint32_t _rand();
    uint32_t _interval();
    void _pickDelta();
};
