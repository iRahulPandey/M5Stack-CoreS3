#pragma once

#include <stdint.h>

// Minimal driver for the LTR-553 proximity sensor at the top of the screen: is a hand close?
// Call update() with the internal I2C bus locked.
class Proximity {
public:
    bool begin();
    void update(uint32_t nowMs);

    bool enabled() const { return _enabled; }
    bool near() const { return _near; }
    uint16_t raw() const { return _raw; }
    uint16_t baseline() const { return static_cast<uint16_t>(_baseline); }

private:
    bool _enabled = false;
    bool _near = false;
    bool _primed = false;
    uint16_t _raw = 0;
    float _baseline = 0.0f;
    uint32_t _nextReadMs = 0;
};
