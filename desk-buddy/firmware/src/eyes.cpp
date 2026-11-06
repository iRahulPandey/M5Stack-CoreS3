#include "eyes.h"

#include <esp_random.h>
#include <math.h>

#include "theme.h"

using buddy::Mood;

namespace {

constexpr float CENTER_X = 160.0f;
constexpr float CENTER_Y = 118.0f;
constexpr float HALF_GAP = 47.0f;        // from the screen centre to each eye's centre
constexpr float EASE = 0.22f;            // share of the way to the target pose per frame
constexpr float MIN_EYE_H = 6.0f;
constexpr float PI_F = 3.14159265f;

constexpr uint32_t BLINK_MS = 140;
constexpr uint32_t BLINK_GAP_MIN_MS = 1800;
constexpr uint32_t BLINK_GAP_MAX_MS = 5500;
constexpr uint32_t GLANCE_GAP_MIN_MS = 900;
constexpr uint32_t GLANCE_GAP_MAX_MS = 3200;
constexpr float GLANCE_X = 30.0f;
constexpr float GLANCE_Y = 14.0f;
constexpr float LEAN_LOOK_X = 70.0f;     // how far the eyes roll toward a tilt
constexpr float LEAN_LOOK_Y = 40.0f;
constexpr float MAX_LOOK_X = 45.0f;
constexpr float MAX_LOOK_Y = 30.0f;
// The camera sees you un-mirrored (your left is on the image's right), and the screen faces you,
// so the eyes move opposite to your position in the image to look at you. Flip if they look away.
constexpr float FACE_LOOK_SIGN = -1.0f;
constexpr float FACE_LOOK_X = 45.0f;
constexpr float FACE_LOOK_Y = 25.0f;

uint32_t randomBetween(uint32_t lo, uint32_t hi) {
    return lo + esp_random() % (hi - lo);
}

float randomSigned(float range) {
    return (static_cast<float>(esp_random() % 2001) / 1000.0f - 1.0f) * range;
}

float approach(float from, float to) {
    return from + (to - from) * EASE;
}

float clampf(float v, float limit) {
    return v > limit ? limit : (v < -limit ? -limit : v);
}

float wave(uint32_t nowMs, uint32_t periodMs) {
    return sinf(2.0f * PI_F * static_cast<float>(nowMs % periodMs) / periodMs);
}

bool blinksIn(Mood m) {
    return m == Mood::Idle || m == Mood::Bored || m == Mood::Curious || m == Mood::Angry;
}

void drawRectEye(M5Canvas& c, float cx, float cy, float w, float h, float r, float lid, float slant, bool left,
                 uint16_t color) {
    h = fmaxf(h, MIN_EYE_H);
    r = fminf(r, fminf(w, h) / 2.0f);
    int x = lroundf(cx - w / 2), y = lroundf(cy - h / 2);
    int iw = lroundf(w), ih = lroundf(h);
    c.fillRoundRect(x, y, iw, ih, lroundf(r), color);
    if (ih > 40 && slant < 0.05f) {  // the glint; angry eyes look meaner without it
        int s = lroundf(w * 0.18f);
        c.fillRoundRect(lroundf(cx + w * 0.14f), y + lroundf(h * 0.12f), s, s, 3, theme::BLACK);
    }
    if (lid > 0.01f) c.fillRect(x - 2, y - 2, iw + 4, lroundf(lid * h) + 2, theme::BLACK);
    if (slant > 0.01f) {
        int drop = y + lroundf(slant * h);
        if (left) {
            c.fillTriangle(x - 2, y - 2, x + iw + 2, y - 2, x + iw + 2, drop, theme::BLACK);
        } else {
            c.fillTriangle(x - 2, y - 2, x + iw + 2, y - 2, x - 2, drop, theme::BLACK);
        }
    }
}

void drawHappyEye(M5Canvas& c, float cx, float cy) {
    constexpr float R_IN = 22.0f, R_OUT = 38.0f, A0 = 200.0f, A1 = 340.0f;
    float oy = cy + 18.0f;
    c.fillArc(lroundf(cx), lroundf(oy), R_IN, R_OUT, A0, A1, theme::EYE);
    for (float a : {A0, A1}) {  // round off both ends of the arc
        float rad = a * PI_F / 180.0f, mid = (R_IN + R_OUT) / 2.0f;
        c.fillCircle(lroundf(cx + cosf(rad) * mid), lroundf(oy + sinf(rad) * mid), 8, theme::EYE);
    }
}

void drawSpiral(M5Canvas& c, float cx, float cy, float phase) {
    float px = cx, py = cy;
    for (float t = 0.25f; t < 4.0f * PI_F; t += 0.25f) {
        float r = 3.0f + t * 3.3f, a = t + phase;
        float x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        c.drawWideLine(px, py, x, y, 3.5f, theme::EYE);
        px = x;
        py = y;
    }
}

void drawHeart(M5Canvas& c, float cx, float cy, float s) {
    c.fillCircle(lroundf(cx - s * 0.5f), lroundf(cy - s * 0.2f), lroundf(s * 0.55f), theme::LOVE);
    c.fillCircle(lroundf(cx + s * 0.5f), lroundf(cy - s * 0.2f), lroundf(s * 0.55f), theme::LOVE);
    c.fillTriangle(lroundf(cx - s * 1.04f), lroundf(cy), lroundf(cx + s * 1.04f), lroundf(cy), lroundf(cx),
                   lroundf(cy + s * 1.15f), theme::LOVE);
}

// A cartoon hand by the bottom-right corner, rocking from the wrist.
void drawWavingHand(M5Canvas& c, uint32_t nowMs) {
    constexpr float WRIST_X = 268.0f, WRIST_Y = 236.0f;
    float a = (-18.0f + 22.0f * wave(nowMs, 500)) * PI_F / 180.0f;
    // hand space: `along` points from the wrist to the fingertips, `across` to the hand's right
    auto at = [&](float along, float across, float& x, float& y) {
        x = WRIST_X + along * sinf(a) + across * cosf(a);
        y = WRIST_Y - along * cosf(a) + across * sinf(a);
    };
    float x0, y0, x1, y1;
    at(14, 0, x1, y1);
    c.drawWideLine(WRIST_X, WRIST_Y, x1, y1, 10.0f, theme::EYE);
    at(40, 0, x0, y0);
    c.fillCircle(lroundf(x0), lroundf(y0), 22, theme::EYE);
    for (float s : {-15.0f, -5.0f, 5.0f, 15.0f}) {
        at(52, s, x0, y0);
        at(78, s * 1.25f, x1, y1);
        c.drawWideLine(x0, y0, x1, y1, 5.5f, theme::EYE);
    }
    at(36, -18, x0, y0);
    at(50, -36, x1, y1);
    c.drawWideLine(x0, y0, x1, y1, 5.5f, theme::EYE);
}

void drawAsleep(M5Canvas& c, uint32_t nowMs) {
    float breathe = 1.0f + 0.06f * wave(nowMs, 3200);
    for (float side : {-1.0f, 1.0f}) {
        float w = 70.0f * breathe;
        c.fillRoundRect(lroundf(CENTER_X + side * HALF_GAP - w / 2), lroundf(CENTER_Y + 10), lroundf(w), 8, 4,
                        theme::EYE);
    }
    c.setFont(&fonts::FreeSansBold12pt7b);
    c.setTextColor(theme::SLEEP_Z);
    c.setTextDatum(middle_center);
    for (int i = 0; i < 3; i++) {  // three Zs drifting up and growing, staggered
        float p = fmodf(nowMs / 1000.0f + i * 0.9f, 2.7f) / 2.7f;
        c.setTextSize(0.8f + p);
        c.drawString("z", lroundf(240 + p * 30), lroundf(100 - p * 70));
    }
    c.setTextSize(1);
}

}  // namespace

void Eyes::setMood(Mood mood, uint32_t nowMs) {
    if (mood == _mood) return;
    _mood = mood;
    _moodSinceMs = nowMs;
}

Eyes::Pose Eyes::targetFor(Mood mood) const {
    Pose p;
    switch (mood) {
        case Mood::Bored: p.lid = 0.55f; p.lookX = -26.0f; p.lookY = 10.0f; break;
        case Mood::Angry: p.lid = 0.12f; p.slant = 0.42f; p.h = 92.0f; break;
        case Mood::Surprised: p.w = 88.0f; p.h = 88.0f; p.r = 44.0f; p.lookY = -6.0f; break;
        case Mood::Curious: p.leftScale = 1.15f; p.rightScale = 0.86f; break;
        default: break;
    }
    bool watchesYou = _faceVisible && mood != Mood::Asleep && mood != Mood::Dizzy && mood != Mood::Excited;
    if (watchesYou) {  // look straight at the person instead of glancing around
        p.lookX = clampf(FACE_LOOK_SIGN * _faceX * FACE_LOOK_X, MAX_LOOK_X);
        p.lookY = clampf(_faceY * FACE_LOOK_Y, MAX_LOOK_Y);
        return p;
    }
    if (mood == Mood::Idle) {
        p.lookX = _glanceX;
        p.lookY = _glanceY;
    }
    if (mood == Mood::Idle || mood == Mood::Curious) {  // roll toward wherever it is tilted
        p.lookX = clampf(p.lookX + _leanX * LEAN_LOOK_X, MAX_LOOK_X);
        p.lookY = clampf(p.lookY + _leanY * LEAN_LOOK_Y, MAX_LOOK_Y);
    }
    return p;
}

void Eyes::update(const buddy::MotionState& motion, const CameraState& camera, uint32_t nowMs) {
    _energy = motion.energy;
    _leanX = motion.leanX;
    _leanY = motion.leanY;
    _faceVisible = camera.faceVisible;
    _faceX = camera.faceX;
    _faceY = camera.faceY;
    if (!_faceVisible) updateGlance(nowMs);
    updateBlink(nowMs);
    Pose t = targetFor(_mood);
    _pose.w = approach(_pose.w, t.w);
    _pose.h = approach(_pose.h, t.h);
    _pose.r = approach(_pose.r, t.r);
    _pose.lookX = approach(_pose.lookX, t.lookX);
    _pose.lookY = approach(_pose.lookY, t.lookY);
    _pose.lid = approach(_pose.lid, t.lid);
    _pose.slant = approach(_pose.slant, t.slant);
    _pose.leftScale = approach(_pose.leftScale, t.leftScale);
    _pose.rightScale = approach(_pose.rightScale, t.rightScale);
}

void Eyes::updateGlance(uint32_t nowMs) {
    if (nowMs < _nextGlanceMs) return;
    _nextGlanceMs = nowMs + randomBetween(GLANCE_GAP_MIN_MS, GLANCE_GAP_MAX_MS);
    bool backToCenter = esp_random() % 3 == 0;
    _glanceX = backToCenter ? 0.0f : randomSigned(GLANCE_X);
    _glanceY = backToCenter ? 0.0f : randomSigned(GLANCE_Y);
}

void Eyes::updateBlink(uint32_t nowMs) {
    if (_blinking && nowMs - _blinkStartMs >= BLINK_MS) {
        _blinking = false;
        bool doubleBlink = esp_random() % 6 == 0;
        _nextBlinkMs = nowMs + (doubleBlink ? 120 : randomBetween(BLINK_GAP_MIN_MS, BLINK_GAP_MAX_MS));
    }
    if (!_blinking && blinksIn(_mood) && nowMs >= _nextBlinkMs) {
        _blinking = true;
        _blinkStartMs = nowMs;
    }
}

float Eyes::blinkFactor(uint32_t nowMs) const {
    if (!_blinking) return 1.0f;
    float t = static_cast<float>(nowMs - _blinkStartMs) / BLINK_MS;
    return 1.0f - 0.92f * sinf(PI_F * fminf(t, 1.0f));
}

void Eyes::drawOpenEyes(M5Canvas& c, uint32_t nowMs) const {
    bool angry = _mood == Mood::Angry;
    uint16_t color = angry ? theme::ANGRY : theme::EYE;
    float jitterX = angry ? randomSigned(2.0f) : 0.0f, jitterY = angry ? randomSigned(2.0f) : 0.0f;
    float blink = blinkFactor(nowMs);
    float cy = CENTER_Y + _pose.lookY + jitterY;
    for (int i = 0; i < 2; i++) {
        bool left = i == 0;
        float scale = left ? _pose.leftScale : _pose.rightScale;
        float cx = CENTER_X + (left ? -HALF_GAP : HALF_GAP) + _pose.lookX + jitterX;
        drawRectEye(c, cx, cy, _pose.w * scale, _pose.h * scale * blink, _pose.r * scale, _pose.lid, _pose.slant, left,
                    color);
    }
}

// Bounces and squashes with the shake: the harder the shake, the bigger the bounce.
void Eyes::drawExcited(M5Canvas& c, uint32_t nowMs) const {
    float amount = fminf(_energy, 1.5f) / 1.5f;
    float s = wave(nowMs, 220);
    float dy = s * (6.0f + 18.0f * amount);
    for (int i = 0; i < 2; i++) {
        bool left = i == 0;
        float cx = CENTER_X + (left ? -HALF_GAP : HALF_GAP) + (left ? -s : s) * 4.0f;
        drawRectEye(c, cx, CENTER_Y + dy, 70.0f * (1.0f - 0.08f * s), 100.0f * (1.0f + 0.12f * s), 22.0f, 0, 0, left,
                    theme::EYE);
    }
}

void Eyes::draw(M5Canvas& c, uint32_t nowMs) const {
    c.fillScreen(theme::BLACK);
    switch (_mood) {
        case Mood::Happy:
        case Mood::Waving: {
            float dy = 4.0f * wave(nowMs, 600);
            drawHappyEye(c, CENTER_X - HALF_GAP + _pose.lookX, CENTER_Y + _pose.lookY + dy);
            drawHappyEye(c, CENTER_X + HALF_GAP + _pose.lookX, CENTER_Y + _pose.lookY + dy);
            if (_mood == Mood::Waving) drawWavingHand(c, nowMs);
            break;
        }
        case Mood::Dizzy: {
            float phase = nowMs * 0.012f;
            float ox = 6.0f * cosf(phase * 0.5f), oy = 6.0f * sinf(phase * 0.5f);
            drawSpiral(c, CENTER_X - HALF_GAP + ox, CENTER_Y + oy, phase);
            drawSpiral(c, CENTER_X + HALF_GAP - ox, CENTER_Y - oy, phase);
            break;
        }
        case Mood::Loved: {
            float s = 30.0f * (1.0f + 0.08f * wave(nowMs, 500));
            drawHeart(c, CENTER_X - HALF_GAP, CENTER_Y - 4, s);
            drawHeart(c, CENTER_X + HALF_GAP, CENTER_Y - 4, s);
            break;
        }
        case Mood::Asleep: drawAsleep(c, nowMs); break;
        case Mood::Excited: drawExcited(c, nowMs); break;
        default: drawOpenEyes(c, nowMs); break;
    }
}
