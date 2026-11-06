#include "proximity.h"

#include <M5Unified.h>

namespace {

constexpr uint8_t ADDRESS = 0x23;
constexpr uint32_t I2C_FREQ = 400000;

// LTR-553ALS registers (datasheet)
constexpr uint8_t PS_CONTR = 0x81;      // bits 1:0 = 11: proximity sensing active
constexpr uint8_t PS_N_PULSES = 0x83;
constexpr uint8_t PS_MEAS_RATE = 0x84;  // 0x00: every 50 ms
constexpr uint8_t PS_DATA_0 = 0x8D;     // low 8 bits; PS_DATA_1 holds bits 10:8
constexpr uint8_t PART_ID = 0x86;
constexpr uint8_t EXPECTED_PART_ID = 0x92;

constexpr uint32_t READ_EVERY_MS = 100;
constexpr float BASELINE_ALPHA = 0.02f;  // drifts with ambient conditions while nothing is near
constexpr float NEAR_ON = 120.0f;        // counts above the baseline that mean "a hand"
constexpr float NEAR_OFF = 60.0f;
constexpr uint16_t DATA_MASK = 0x07FF;

}  // namespace

bool Proximity::begin() {
    if (M5.In_I2C.readRegister8(ADDRESS, PART_ID, I2C_FREQ) != EXPECTED_PART_ID) return false;
    bool ok = M5.In_I2C.writeRegister8(ADDRESS, PS_N_PULSES, 0x04, I2C_FREQ) &&
              M5.In_I2C.writeRegister8(ADDRESS, PS_MEAS_RATE, 0x00, I2C_FREQ) &&
              M5.In_I2C.writeRegister8(ADDRESS, PS_CONTR, 0x03, I2C_FREQ);
    _enabled = ok;
    return ok;
}

void Proximity::update(uint32_t nowMs) {
    if (!_enabled || nowMs < _nextReadMs) return;
    _nextReadMs = nowMs + READ_EVERY_MS;
    uint8_t data[2] = {0, 0};
    if (!M5.In_I2C.readRegister(ADDRESS, PS_DATA_0, data, sizeof(data), I2C_FREQ)) return;
    _raw = static_cast<uint16_t>(((data[1] << 8) | data[0]) & DATA_MASK);

    if (!_primed) {
        _baseline = _raw;
        _primed = true;
    }
    float above = _raw - _baseline;
    if (_near) {
        _near = above > NEAR_OFF;
    } else {
        _near = above > NEAR_ON;
        if (!_near) _baseline += BASELINE_ALPHA * (_raw - _baseline);
    }
}
