#include "level.h"

#include <math.h>

namespace ball {

namespace {

constexpr float MIN_READING_G = 0.1f;  // weaker than this (in the air) there is no "down" to go by
constexpr float MIN_FLAT = 0.3f;       // an axis this close to the level reading cannot be laid flat
constexpr float SCREEN_RIGHT[3] = {1.0f, 0.0f, 0.0f};
constexpr float SCREEN_TOP[3] = {0.0f, 1.0f, 0.0f};

float dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// `v` with its part along the unit vector `up` taken out, scaled back to unit length.
bool layFlat(const float v[3], const float up[3], float out[3]) {
    float along = dot(v, up);
    for (int i = 0; i < 3; i++) out[i] = v[i] - along * up[i];
    float length = sqrtf(dot(out, out));
    if (length < MIN_FLAT) return false;
    for (int i = 0; i < 3; i++) out[i] /= length;
    return true;
}

bool unit(float ax, float ay, float az, float out[3]) {
    float length = sqrtf(ax * ax + ay * ay + az * az);
    if (length < MIN_READING_G) return false;
    out[0] = ax / length;
    out[1] = ay / length;
    out[2] = az / length;
    return true;
}

}  // namespace

Level::Level() {
    set(0.0f, 0.0f, 1.0f);
}

void Level::set(float ax, float ay, float az) {
    float up[3];
    if (!unit(ax, ay, az, up)) return;
    float right[3], top[3];
    if (layFlat(SCREEN_RIGHT, up, right)) {
        cross(up, right, top);
    } else {  // lying on its left or right side: start from the screen's top edge instead
        layFlat(SCREEN_TOP, up, top);
        cross(top, up, right);
    }
    for (int i = 0; i < 3; i++) {
        _right[i] = right[i];
        _top[i] = top[i];
    }
}

// The reading leans towards whichever side is raised, and the ball rolls the other way.
Vec2 Level::pull(float ax, float ay, float az) const {
    float a[3];
    if (!unit(ax, ay, az, a)) return Vec2{};
    return Vec2{-dot(a, _right), dot(a, _top)};
}

}  // namespace ball
