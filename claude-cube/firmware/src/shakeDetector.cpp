#include "shakeDetector.h"

#include <M5Unified.h>
#include <math.h>

namespace {

constexpr float JOLT_G = 0.9f;          // deviation from 1 g that counts as a jolt
constexpr uint8_t JOLTS_PER_SHAKE = 2;
constexpr uint32_t SHAKE_WINDOW_MS = 500;
constexpr uint32_t COOLDOWN_MS = 700;   // one page per shake

}  // namespace

bool ShakeDetector::update(uint32_t nowMs) {
    if (!M5.Imu.isEnabled()) {
        return false;
    }
    M5.Imu.update();
    float ax = 0, ay = 0, az = 0;
    M5.Imu.getAccel(&ax, &ay, &az);
    bool jolt = fabsf(sqrtf(ax * ax + ay * ay + az * az) - 1.0f) > JOLT_G;

    bool risingEdge = jolt && !_inJolt;
    _inJolt = jolt;
    if (!risingEdge || nowMs - _lastShakeMs < COOLDOWN_MS) {
        return false;
    }
    if (_jolts == 0 || nowMs - _firstJoltMs > SHAKE_WINDOW_MS) {
        _jolts = 0;
        _firstJoltMs = nowMs;
    }
    if (++_jolts < JOLTS_PER_SHAKE) {
        return false;
    }
    _jolts = 0;
    _lastShakeMs = nowMs;
    return true;
}
