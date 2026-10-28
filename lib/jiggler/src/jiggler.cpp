#include "jiggler.h"

uint32_t Jiggler::_rand() {
    uint32_t x = _rng; // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    _rng = x;
    return x;
}

uint32_t Jiggler::_interval() {
    uint32_t minMs = (uint32_t)_minSec * 1000UL;
    uint32_t maxMs = (uint32_t)_maxSec * 1000UL;
    if (maxMs <= minMs) {
        return minMs;
    }
    uint32_t span = (maxMs - minMs) + 1UL;
    return minMs + (_rand() % span);
}

void Jiggler::_pickDelta() {
    do {
        _lastDx = (int8_t)((int32_t)(_rand() % 9) - 4);
        _lastDy = (int8_t)((int32_t)(_rand() % 9) - 4);
    } while (_lastDx == 0 && _lastDy == 0);
}

void Jiggler::begin(uint32_t now, uint32_t seed) {
    _rng = seed ? seed : 0x9E3779B9UL;
    _active = true;
    _count = 0;
    _subStep = 0;
    _lastDx = _lastDy = 0;
    _next = now + _interval();
}

void Jiggler::toggle(uint32_t now) {
    setActive(now, !_active);
}

void Jiggler::setActive(uint32_t now, bool enable) {
    if (_active == enable) return;
    _active = enable;
    _subStep = 0;
    if (_active) {
        _next = now + 2000UL; // kurzer Vorlauf nach Aktivierung
    }
}

void Jiggler::configure(uint32_t now, uint16_t minSec, uint16_t maxSec, JiggleMode mode) {
    if (minSec < 2) minSec = 2;
    if (maxSec < minSec) maxSec = minSec;
    if (maxSec > 3600) maxSec = 3600;
    _minSec = minSec;
    _maxSec = maxSec;
    _mode = mode;
    _subStep = 0;
    if (_active) {
        _next = now + _interval();
    }
}

bool Jiggler::poll(uint32_t now, int8_t &dx, int8_t &dy) {
    if (!_active || (int32_t)(now - _next) < 0) {
        return false;
    }

    if (_mode == JiggleMode::PING_PONG) {
        if (_subStep == 1) {
            dx = -_lastDx;
            dy = -_lastDy;
            _subStep = 0;
        } else {
            _pickDelta();
            dx = _lastDx;
            dy = _lastDy;
            _subStep = 1;
        }
        _count++;
        _next = now + _interval();
        return true;
    }

    if (_mode == JiggleMode::INSTANT_RETURN) {
        if (_subStep == 0) {
            _pickDelta();
            dx = _lastDx;
            dy = _lastDy;
            _subStep = 1;
            _count++;
            _next = now + 35UL; // 35 ms später sofort zurück
        } else {
            dx = -_lastDx;
            dy = -_lastDy;
            _subStep = 0;
            _next = now + _interval();
        }
        return true;
    }

    // NATURAL_ARC: 4 Mikro-Schritte im Quadrat/Bogen, Summe = (0,0)
    switch (_subStep) {
        case 0:
            _pickDelta();
            dx = _lastDx;
            dy = _lastDy;
            _subStep = 1;
            _count++;
            _next = now + 25UL;
            break;
        case 1:
            dx = _lastDy;
            dy = -_lastDx;
            _subStep = 2;
            _next = now + 25UL;
            break;
        case 2:
            dx = -_lastDx;
            dy = -_lastDy;
            _subStep = 3;
            _next = now + 25UL;
            break;
        default:
            dx = -_lastDy;
            dy = _lastDx;
            _subStep = 0;
            _next = now + _interval();
            break;
    }
    return true;
}

uint32_t Jiggler::msUntilNext(uint32_t now) const {
    if (!_active || (int32_t)(_next - now) <= 0) {
        return 0;
    }
    return _next - now;
}
