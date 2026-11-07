#include "gravity.h"

#include <math.h>

namespace sand {

namespace {

constexpr float MIN_READING_G = 0.1f;   // weaker than this (in the air) there is no "down" to go by
constexpr float NO_PULL = 0.001f;
constexpr float EIGHTH_TURN = 3.14159265f / 4.0f;
constexpr float STEPS_PER_S_AT_1G = 75.0f;
constexpr float MIN_STRENGTH = 0.2f;    // about 12 degrees off flat: below this the sand stays put
constexpr int MAX_STEPS = 4;            // per frame: after a stall carry on, rather than catch up

}  // namespace

Gravity gravityFrom(float ax, float ay, float az) {
    float length = sqrtf(ax * ax + ay * ay + az * az);
    if (length < MIN_READING_G) return Gravity{};
    // Down is opposite the reading; on the screen x runs right (as the IMU's x) and y runs down
    // (against the IMU's y).
    float gx = -ax / length, gy = ay / length;
    Gravity g;
    g.strength = hypotf(gx, gy);
    if (g.strength < NO_PULL) return g;
    float angle = lroundf(atan2f(gy, gx) / EIGHTH_TURN) * EIGHTH_TURN;  // the nearest of the 8 neighbours
    g.down.dx = static_cast<int8_t>(lroundf(cosf(angle)));
    g.down.dy = static_cast<int8_t>(lroundf(sinf(angle)));
    return g;
}

int Pacer::steps(float strength, float dtS) {
    if (strength < MIN_STRENGTH) {
        _owed = 0.0f;
        return 0;
    }
    _owed += strength * STEPS_PER_S_AT_1G * dtS;
    int n = static_cast<int>(_owed);
    if (n > MAX_STEPS) {
        _owed = 0.0f;
        return MAX_STEPS;
    }
    _owed -= n;
    return n;
}

}  // namespace sand
