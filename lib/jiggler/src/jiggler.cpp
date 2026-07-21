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
    return 30000UL + (_rand() % 30001UL); // 30..60 s
}

void Jiggler::begin(uint32_t now, uint32_t seed) {
    _rng = seed ? seed : 0x9E3779B9UL;
    _active = true;
    _count = 0;
    _pendingReturn = false;
    _lastDx = _lastDy = 0;
    _next = now + _interval();
}

void Jiggler::toggle(uint32_t now) {
    _active = !_active;
    if (_active) {
        _next = now + 2000UL; // kurzer Vorlauf nach Reaktivierung
    }
}

bool Jiggler::active() const {
    return _active;
}

uint32_t Jiggler::count() const {
    return _count;
}

bool Jiggler::poll(uint32_t now, int8_t &dx, int8_t &dy) {
    if (!_active || (int32_t)(now - _next) < 0) {
        return false;
    }
    if (_pendingReturn) {
        dx = -_lastDx;
        dy = -_lastDy;
    } else {
        do {
            dx = (int8_t)((int32_t)(_rand() % 9) - 4);
            dy = (int8_t)((int32_t)(_rand() % 9) - 4);
        } while (dx == 0 && dy == 0);
        _lastDx = dx;
        _lastDy = dy;
    }
    _pendingReturn = !_pendingReturn;
    _count++;
    _next = now + _interval();
    return true;
}

uint32_t Jiggler::msUntilNext(uint32_t now) const {
    if (!_active || (int32_t)(_next - now) <= 0) {
        return 0;
    }
    return _next - now;
}
