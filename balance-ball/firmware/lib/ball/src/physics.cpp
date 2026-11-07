#include "physics.h"

#include <math.h>

namespace ball {

namespace {

constexpr float PULL_PX_PER_S2 = 1400.0f;  // per g: a 10 degree lean rolls it to the side in about a second
constexpr float HILL_PER_S2 = 0.8f;        // push away from the middle, per pixel off it
constexpr float FRICTION_PER_S = 0.9f;
constexpr float BOUNCE = 0.35f;            // the share of its speed a wall gives back
constexpr float REST_SPEED = 20.0f;        // bouncing back slower than this, it stays against the wall
constexpr float STEP_S = 0.005f;           // small steps, so a slow frame cannot carry it through a wall
constexpr float TOUCH_PX = 0.5f;

float bounced(float speed) {
    float back = -speed * BOUNCE;
    return fabsf(back) < REST_SPEED ? 0.0f : back;
}

// Keeps one coordinate between its two walls.
void keepWithin(float& pos, float& speed, float low, float high) {
    if (pos < low) {
        pos = low;
        if (speed < 0.0f) speed = bounced(speed);
    } else if (pos > high) {
        pos = high;
        if (speed > 0.0f) speed = bounced(speed);
    }
}

}  // namespace

Ball centered(const Arena& arena) {
    Ball b;
    b.x = arena.width / 2.0f;
    b.y = arena.height / 2.0f;
    return b;
}

Ball roll(const Ball& ball, const Arena& arena, Vec2 pull, float dtS) {
    Ball b = ball;
    const float cx = arena.width / 2.0f, cy = arena.height / 2.0f;
    const int steps = dtS > 0.0f ? static_cast<int>(ceilf(dtS / STEP_S)) : 0;
    const float dt = steps > 0 ? dtS / steps : 0.0f;
    const float keep = 1.0f - FRICTION_PER_S * dt;
    for (int i = 0; i < steps; i++) {
        b.vx = (b.vx + (pull.x * PULL_PX_PER_S2 + HILL_PER_S2 * (b.x - cx)) * dt) * keep;
        b.vy = (b.vy + (pull.y * PULL_PX_PER_S2 + HILL_PER_S2 * (b.y - cy)) * dt) * keep;
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        keepWithin(b.x, b.vx, arena.radius, arena.width - arena.radius);
        keepWithin(b.y, b.vy, arena.radius, arena.height - arena.radius);
    }
    return b;
}

uint8_t wallsTouched(const Ball& b, const Arena& arena) {
    uint8_t walls = 0;
    if (b.x <= arena.radius + TOUCH_PX) walls |= WALL_LEFT;
    if (b.x >= arena.width - arena.radius - TOUCH_PX) walls |= WALL_RIGHT;
    if (b.y <= arena.radius + TOUCH_PX) walls |= WALL_TOP;
    if (b.y >= arena.height - arena.radius - TOUCH_PX) walls |= WALL_BOTTOM;
    return walls;
}

float fromCenter(const Ball& b, const Arena& arena) {
    return hypotf(b.x - arena.width / 2.0f, b.y - arena.height / 2.0f);
}

float wallGap(const Ball& b, const Arena& arena) {
    float gap = fminf(fminf(b.x - arena.radius, arena.width - arena.radius - b.x),
                      fminf(b.y - arena.radius, arena.height - arena.radius - b.y));
    return fmaxf(gap, 0.0f);
}

}  // namespace ball
