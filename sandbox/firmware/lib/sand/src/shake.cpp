#include "shake.h"

#include <math.h>

namespace sand {

namespace {

constexpr float ENERGY_ALPHA = 0.05f;  // ~200 ms at 100 Hz
constexpr float START_G = 0.3f;
constexpr float STOP_G = 0.15f;

}  // namespace

void ShakeDetector::update(float ax, float ay, float az) {
    float off = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f);
    _energy += ENERGY_ALPHA * (off - _energy);
    if (!_shaking && _energy > START_G) {
        _shaking = true;
    } else if (_shaking && _energy < STOP_G) {
        _shaking = false;
    }
}

}  // namespace sand
