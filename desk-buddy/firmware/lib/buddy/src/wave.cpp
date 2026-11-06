#include "wave.h"

#include <math.h>
#include <stdlib.h>

namespace buddy {

namespace {

constexpr float MIN_AMOUNT = 0.02f;      // less than this is noise
constexpr float MAX_AMOUNT = 0.5f;       // more than this is the whole picture changing, not a hand
constexpr float MIN_SWING = 0.08f;       // a swing must cover this share of the image width
constexpr uint32_t WINDOW_MS = 2000;     // the turns must happen within this long
constexpr int TURNS_FOR_WAVE = 3;        // left-right-left-right
constexpr uint32_t LOST_AFTER_MS = 400;  // motion gone this long: start over
constexpr uint32_t COOLDOWN_MS = 3000;   // one wave per greeting

}  // namespace

FrameMotion diffFrames(const uint8_t* previous, const uint8_t* current, int w, int h, uint8_t threshold) {
    FrameMotion m;
    long changed = 0;
    float sumX = 0.0f, sumY = 0.0f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            if (abs(static_cast<int>(current[i]) - static_cast<int>(previous[i])) > threshold) {
                changed++;
                sumX += x + 0.5f;
                sumY += y + 0.5f;
            }
        }
    }
    if (changed == 0) return m;
    m.amount = static_cast<float>(changed) / static_cast<float>(w * h);
    m.centerX = sumX / changed / w;
    m.centerY = sumY / changed / h;
    return m;
}

bool WaveDetector::update(const FrameMotion& motion, uint32_t nowMs) {
    bool usable = motion.amount >= MIN_AMOUNT && motion.amount <= MAX_AMOUNT;
    if (!usable) {
        if (_tracking && nowMs - _lastSeenMs > LOST_AFTER_MS) reset();
        return false;
    }
    _lastSeenMs = nowMs;
    float x = motion.centerX;
    if (!_tracking) {
        _tracking = true;
        _anchorX = _lastX = x;
        _direction = 0;
        return false;
    }
    int direction = x > _lastX ? 1 : (x < _lastX ? -1 : _direction);
    if (_direction != 0 && direction != _direction) {
        if (fabsf(_lastX - _anchorX) >= MIN_SWING && _turns < MAX_TURNS) _turnsMs[_turns++] = nowMs;
        _anchorX = _lastX;  // the next swing starts where this one turned
    }
    _direction = direction;
    _lastX = x;

    int recent = 0;
    for (int i = 0; i < _turns; i++) {
        if (nowMs - _turnsMs[i] <= WINDOW_MS) recent++;
    }
    if (recent < TURNS_FOR_WAVE || nowMs < _cooldownUntilMs) return false;
    reset();
    _cooldownUntilMs = nowMs + COOLDOWN_MS;
    return true;
}

void WaveDetector::reset() {
    _tracking = false;
    _direction = 0;
    _turns = 0;
}

}  // namespace buddy
