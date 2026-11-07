#include "mood.h"

#include <math.h>

namespace ball {

namespace {

constexpr float WORRY_GAP_PX = 45.0f;     // a wall this close is worrying...
constexpr float PANIC_GAP_PX = 15.0f;     // ...and this close, alarming
constexpr uint32_t BLISS_AFTER_MS = 3000;
constexpr uint32_t ANGRY_HOLD_MS = 1200;  // still angry this long after leaving the wall
constexpr uint32_t FUME_AFTER_MS = 1500;  // pressed against it this long: fuming
constexpr float REDDEN_MS = 250.0f;       // from its usual colour to red...
constexpr float COOL_MS = 3000.0f;        // ...and back
constexpr float SULK_ABOVE = 0.15f;       // still this red once the anger has passed: sulking
constexpr uint32_t SETTLE_MS = 250;       // a calm mood has to last this long before it shows

bool isAnger(Mood mood) {
    return mood == Mood::Angry || mood == Mood::Fuming;
}

}  // namespace

const char* moodName(Mood mood) {
    switch (mood) {
        case Mood::Blissful: return "blissful";
        case Mood::Happy: return "happy";
        case Mood::Content: return "content";
        case Mood::Worried: return "worried";
        case Mood::Panicking: return "panicking";
        case Mood::Angry: return "angry";
        case Mood::Fuming: return "fuming";
        case Mood::Sulking: return "sulking";
    }
    return "happy";
}

bool MoodEngine::update(const Situation& now, uint32_t nowMs) {
    if (!_started) {
        _started = true;
        _lastMs = _moodSinceMs = nowMs;
    }
    trackStreak(now, nowMs);
    trackTouch(now, nowMs);
    trackRedness(nowMs);
    _lastMs = nowMs;
    return settle(wanted(now, nowMs), nowMs);
}

uint32_t MoodEngine::streakMs(uint32_t nowMs) const {
    return _inMiddle ? nowMs - _middleSinceMs : 0;
}

void MoodEngine::trackStreak(const Situation& now, uint32_t nowMs) {
    bool inMiddle = now.fromCenterPx <= MIDDLE_PX;
    if (inMiddle && !_inMiddle) _middleSinceMs = nowMs;
    _inMiddle = inMiddle;
    if (inMiddle && nowMs - _middleSinceMs > _bestMs) _bestMs = nowMs - _middleSinceMs;
}

void MoodEngine::trackTouch(const Situation& now, uint32_t nowMs) {
    if (now.touching && !_touching) {
        _bumps++;
        _touchSinceMs = nowMs;
    }
    if (now.touching) _lastTouchMs = nowMs;
    _touching = now.touching;
}

bool MoodEngine::angry(uint32_t nowMs) const {
    return _touching || (_bumps > 0 && nowMs - _lastTouchMs < ANGRY_HOLD_MS);
}

void MoodEngine::trackRedness(uint32_t nowMs) {
    float dt = static_cast<float>(nowMs - _lastMs);
    _redness = angry(nowMs) ? fminf(1.0f, _redness + dt / REDDEN_MS) : fmaxf(0.0f, _redness - dt / COOL_MS);
}

Mood MoodEngine::wanted(const Situation& now, uint32_t nowMs) const {
    if (_touching && nowMs - _touchSinceMs >= FUME_AFTER_MS) return Mood::Fuming;
    if (angry(nowMs)) return Mood::Angry;
    if (_redness > SULK_ABOVE) return Mood::Sulking;
    if (_inMiddle) return nowMs - _middleSinceMs >= BLISS_AFTER_MS ? Mood::Blissful : Mood::Happy;
    if (now.wallGapPx < PANIC_GAP_PX) return Mood::Panicking;
    if (now.wallGapPx < WORRY_GAP_PX) return Mood::Worried;
    return Mood::Content;
}

// Anger shows at once, and so does its passing. Any other change shows once the ball has wanted
// something other than its current mood for SETTLE_MS, whatever it wants by then: a ball racing
// across several zones keeps up, and one brushing past a boundary and back does not flicker.
bool MoodEngine::settle(Mood wanted, uint32_t nowMs) {
    if (wanted == _mood) {
        _restless = false;
        return false;
    }
    if (!isAnger(wanted) && !isAnger(_mood)) {
        if (!_restless) {
            _restless = true;
            _restlessSinceMs = nowMs;
        }
        if (nowMs - _restlessSinceMs < SETTLE_MS) return false;
    }
    _mood = wanted;
    _restless = false;
    _moodSinceMs = nowMs;
    return true;
}

}  // namespace ball
