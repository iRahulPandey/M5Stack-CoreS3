#include "face.h"

#include <math.h>

#include "physics.h"
#include "theme.h"

using ball::Mood;

namespace {

constexpr float SQUASH = 0.12f;  // flattened by this share of its radius against a wall
constexpr uint32_t BLINK_EVERY_MS = 3700;
constexpr uint32_t BLINK_MS = 130;
constexpr uint32_t IMPACT_SHAKE_MS = 400;
constexpr uint32_t STEAM_MS = 800;
constexpr int STEAM_PUFFS = 3;

// The body once squashed against walls and shaken by its mood.
struct Shape {
    float x, y, rx, ry;
};

// Where the features go: the face turns a little towards where the ball would rather be.
struct Face {
    float x, y, r;
    float lookX, lookY;
    uint16_t body;
};

bool isAnger(Mood m) {
    return m == Mood::Angry || m == Mood::Fuming;
}

float trembleOf(const BallView& v) {
    if (v.mood == Mood::Fuming) return 2.0f;
    if (v.mood == Mood::Angry && v.moodForMs < IMPACT_SHAKE_MS) return 1.5f;
    if (v.mood == Mood::Panicking) return 0.7f;
    return 0.0f;
}

Shape shapeOf(const BallView& v, uint32_t nowMs) {
    Shape s{v.x, v.y, v.radius, v.radius};
    if (v.walls & (ball::WALL_LEFT | ball::WALL_RIGHT)) {
        s.rx -= v.radius * SQUASH;
        s.ry += v.radius * SQUASH / 2;
        s.x += (v.walls & ball::WALL_LEFT ? -1.0f : 1.0f) * v.radius * SQUASH;  // keep its edge on the wall
    }
    if (v.walls & (ball::WALL_TOP | ball::WALL_BOTTOM)) {
        s.ry -= v.radius * SQUASH;
        s.rx += v.radius * SQUASH / 2;
        s.y += (v.walls & ball::WALL_TOP ? -1.0f : 1.0f) * v.radius * SQUASH;
    }
    float tremble = trembleOf(v);
    s.x += tremble * sinf(nowMs * 0.09f);
    s.y += tremble * cosf(nowMs * 0.13f);
    return s;
}

// Light comes from the top left: a shadow on the table, a darker rim and a shine.
uint16_t drawBody(M5Canvas& c, const Shape& s, float redness) {
    uint16_t body = theme::blend(theme::BODY, theme::RED, redness);
    c.fillEllipse(s.x + 4, s.y + 5, s.rx, s.ry, theme::SHADOW);
    c.fillEllipse(s.x, s.y, s.rx, s.ry, theme::blend(theme::BODY_SHADE, theme::RED_SHADE, redness));
    c.fillEllipse(s.x - s.rx * 0.07f, s.y - s.ry * 0.07f, s.rx * 0.9f, s.ry * 0.9f, body);
    c.fillEllipse(s.x - s.rx * 0.42f, s.y - s.ry * 0.5f, s.rx * 0.2f, s.ry * 0.12f,
                  theme::blend(theme::BODY_SHINE, theme::RED_SHINE, redness));
    return body;
}

Face faceOf(const BallView& v, const Shape& s, uint16_t body) {
    float lookX = v.lookX, lookY = v.lookY;
    if (v.mood == Mood::Happy || v.mood == Mood::Blissful || isAnger(v.mood)) {
        lookX = lookY = 0.0f;  // smiling, or glaring, straight at you
    } else if (v.mood == Mood::Sulking) {
        lookX = -0.7f;  // won't look at you
        lookY = 0.5f;
    }
    float r = fminf(s.rx, s.ry);
    return Face{s.x + lookX * r * 0.12f, s.y + lookY * r * 0.12f, r, lookX, lookY, body};
}

void drawOpenEye(M5Canvas& c, const Face& f, float ex, float ey, Mood mood) {
    bool wide = mood == Mood::Panicking || mood == Mood::Worried;
    float rx = f.r * (wide ? 0.23f : 0.2f), ry = f.r * (wide ? 0.29f : 0.25f);
    float pupil = f.r * (mood == Mood::Panicking ? 0.07f : 0.11f);
    c.fillEllipse(ex, ey, rx, ry, theme::EYE_WHITE);
    c.fillCircle(ex + f.lookX * f.r * 0.08f, ey + f.lookY * f.r * 0.1f, pupil, theme::INK);
}

// Lids for the angry slant (inner corners pulled down) and the sulky half-closed look.
void drawLid(M5Canvas& c, const Face& f, float ex, float ey, float side, Mood mood) {
    float w = f.r * 0.26f;
    if (isAnger(mood)) {
        c.fillTriangle(ex - side * w, ey - f.r * 0.32f, ex + side * w, ey - f.r * 0.32f, ex + side * w, ey - f.r * 0.02f,
                       f.body);
        c.drawWideLine(ex - side * f.r * 0.22f, ey - f.r * 0.27f, ex + side * f.r * 0.2f, ey - f.r * 0.06f, 1.5f,
                       theme::INK);
    } else if (mood == Mood::Sulking) {
        c.fillRect(ex - w, ey - f.r * 0.32f, 2 * w, f.r * 0.32f, f.body);
        c.drawWideLine(ex - f.r * 0.2f, ey, ex + f.r * 0.2f, ey, 1.5f, theme::INK);
    }
}

void drawBrow(M5Canvas& c, const Face& f, float ex, float ey, float side, Mood mood) {
    float outer = ex - side * f.r * 0.22f, inner = ex + side * f.r * 0.16f;
    if (mood == Mood::Worried || mood == Mood::Panicking) {
        c.drawWideLine(outer, ey - f.r * 0.38f, inner, ey - f.r * 0.5f, 2.0f, theme::INK);
    } else if (isAnger(mood)) {
        c.drawWideLine(outer, ey - f.r * 0.52f, inner, ey - f.r * 0.33f, 2.5f, theme::INK);
    } else if (mood == Mood::Sulking) {
        c.drawWideLine(outer, ey - f.r * 0.36f, inner, ey - f.r * 0.32f, 2.0f, theme::INK);
    }
}

void drawEyes(M5Canvas& c, const Face& f, Mood mood, uint32_t nowMs) {
    bool blinking = nowMs % BLINK_EVERY_MS < BLINK_MS && mood != Mood::Panicking && !isAnger(mood);
    for (float side : {1.0f, -1.0f}) {  // left eye, whose inner corner is on its right, then the right eye
        float ex = f.x - side * f.r * 0.36f, ey = f.y - f.r * 0.12f;
        if (mood == Mood::Blissful) {
            c.fillArc(ex, ey + f.r * 0.08f, f.r * 0.17f - 3, f.r * 0.17f, 200, 340, theme::INK);
        } else if (blinking) {
            c.drawWideLine(ex - f.r * 0.18f, ey, ex + f.r * 0.18f, ey, 1.5f, theme::INK);
        } else {
            drawOpenEye(c, f, ex, ey, mood);
            drawLid(c, f, ex, ey, side, mood);
        }
        drawBrow(c, f, ex, ey, side, mood);
    }
}

void drawTeeth(M5Canvas& c, float mx, float my, float w, float h) {
    c.fillRoundRect(mx - w / 2, my - h / 2, w, h, 3, theme::EYE_WHITE);
    c.drawRoundRect(mx - w / 2, my - h / 2, w, h, 3, theme::INK);
    c.drawFastHLine(mx - w / 2, my, w, theme::INK);
    for (int i = 1; i < 4; i++) c.drawFastVLine(mx - w / 2 + w * i / 4, my - h / 2, h, theme::INK);
}

void drawMouth(M5Canvas& c, const Face& f, Mood mood) {
    float mx = f.x, my = f.y + f.r * 0.34f, r = f.r;
    switch (mood) {
        case Mood::Blissful: c.fillArc(mx, my - r * 0.16f, r * 0.32f - 3.5f, r * 0.32f, 20, 160, theme::INK); break;
        case Mood::Happy: c.fillArc(mx, my - r * 0.1f, 0, r * 0.27f, 0, 180, theme::INK); break;
        case Mood::Content: c.fillArc(mx, my - r * 0.12f, r * 0.2f - 3, r * 0.2f, 30, 150, theme::INK); break;
        case Mood::Worried: c.fillEllipse(mx, my, r * 0.07f, r * 0.09f, theme::INK); break;
        case Mood::Panicking: c.fillEllipse(mx, my + r * 0.03f, r * 0.13f, r * 0.17f, theme::INK); break;
        case Mood::Angry: drawTeeth(c, mx, my, r * 0.6f, r * 0.22f); break;
        case Mood::Fuming: drawTeeth(c, mx, my + r * 0.02f, r * 0.72f, r * 0.27f); break;
        case Mood::Sulking: c.fillArc(mx, my + r * 0.16f, r * 0.16f - 3, r * 0.16f, 215, 325, theme::INK); break;
    }
}

void drawSweat(M5Canvas& c, const Face& f) {
    float side = f.lookX > 0.0f ? -1.0f : 1.0f;  // on the side away from where it is looking
    float sx = f.x + side * f.r * 0.74f, sy = f.y - f.r * 0.42f, d = f.r * 0.1f;
    c.fillCircle(sx, sy, d, theme::SWEAT);
    c.fillTriangle(sx - d, sy, sx + d, sy, sx, sy - d * 2.6f, theme::SWEAT);
}

// The comic "popping vein": four brackets curving towards a point, out beside the ball, away from walls.
void drawVein(M5Canvas& c, const Shape& s, uint8_t walls) {
    float vx = s.x + (walls & ball::WALL_RIGHT ? -1.0f : 1.0f) * s.rx * 0.95f;
    float vy = s.y + (walls & ball::WALL_TOP ? 1.0f : -1.0f) * s.ry * 0.95f;
    constexpr int ARM = 8, R = 8;
    c.fillArc(vx - ARM, vy - ARM, R - 3, R, 0, 90, theme::VEIN);
    c.fillArc(vx + ARM, vy - ARM, R - 3, R, 90, 180, theme::VEIN);
    c.fillArc(vx + ARM, vy + ARM, R - 3, R, 180, 270, theme::VEIN);
    c.fillArc(vx - ARM, vy + ARM, R - 3, R, 270, 360, theme::VEIN);
}

void drawSteam(M5Canvas& c, const Shape& s, uint32_t nowMs) {
    for (int i = 0; i < STEAM_PUFFS; i++) {
        float phase = static_cast<float>((nowMs + i * STEAM_MS / STEAM_PUFFS) % STEAM_MS) / STEAM_MS;
        uint16_t color = theme::blend(theme::STEAM, theme::FLOOR, phase);
        for (float side : {-1.0f, 1.0f}) {
            float px = s.x + side * (s.rx * 0.5f + phase * s.rx * 0.35f);
            c.fillCircle(px, s.y - s.ry * 0.85f - phase * s.ry * 0.7f, 3 + phase * 5, color);
        }
    }
}

void drawExtras(M5Canvas& c, const BallView& v, const Shape& s, const Face& f, uint32_t nowMs) {
    if (v.mood == Mood::Blissful || v.mood == Mood::Happy) {
        uint16_t blush = theme::blend(f.body, theme::BLUSH, 0.7f);
        c.fillEllipse(f.x - f.r * 0.56f, f.y + f.r * 0.16f, f.r * 0.12f, f.r * 0.07f, blush);
        c.fillEllipse(f.x + f.r * 0.56f, f.y + f.r * 0.16f, f.r * 0.12f, f.r * 0.07f, blush);
    }
    if (v.mood == Mood::Panicking) drawSweat(c, f);
    if (isAnger(v.mood)) drawVein(c, s, v.walls);
    if (v.mood == Mood::Fuming) drawSteam(c, s, nowMs);
}

}  // namespace

void drawBall(M5Canvas& canvas, const BallView& view, uint32_t nowMs) {
    Shape shape = shapeOf(view, nowMs);
    uint16_t body = drawBody(canvas, shape, view.redness);
    Face face = faceOf(view, shape, body);
    drawEyes(canvas, face, view.mood, nowMs);
    drawMouth(canvas, face, view.mood);
    drawExtras(canvas, view, shape, face, nowMs);
}
