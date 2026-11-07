#pragma once

#include <stdint.h>

namespace ball {

constexpr float MIDDLE_PX = 16.0f;  // its center this close to the screen's counts as in the middle

enum class Mood : uint8_t { Blissful, Happy, Content, Worried, Panicking, Angry, Fuming, Sulking };

const char* moodName(Mood mood);

struct Situation {
    float fromCenterPx = 0.0f;
    float wallGapPx = 0.0f;  // from the ball's edge to the nearest wall
    bool touching = false;   // pressed against a wall
};

// How the ball feels about where it is. It wants to be in the middle: the closer a wall gets, the
// more it worries, and touching one makes it angry and red. Anger lingers, then it sulks while the
// red fades. Calmer changes wait a moment before they show, so the face never flickers.
class MoodEngine {
public:
    bool update(const Situation& now, uint32_t nowMs);  // true when the mood changed
    Mood mood() const { return _mood; }
    uint32_t moodSinceMs() const { return _moodSinceMs; }
    float redness() const { return _redness; }  // 0 is its usual colour, 1 is angry red
    uint32_t streakMs(uint32_t nowMs) const;    // how long it has been in the middle, 0 when it is not
    uint32_t bestMs() const { return _bestMs; }
    uint32_t bumps() const { return _bumps; }   // times it has hit a wall

private:
    void trackStreak(const Situation& now, uint32_t nowMs);
    void trackTouch(const Situation& now, uint32_t nowMs);
    void trackRedness(uint32_t nowMs);
    bool angry(uint32_t nowMs) const;
    Mood wanted(const Situation& now, uint32_t nowMs) const;
    bool settle(Mood wanted, uint32_t nowMs);

    Mood _mood = Mood::Happy;
    uint32_t _moodSinceMs = 0;
    bool _restless = false;  // it has wanted a different mood...
    uint32_t _restlessSinceMs = 0;  // ...since then

    bool _started = false;
    uint32_t _lastMs = 0;
    float _redness = 0.0f;

    bool _inMiddle = false;
    uint32_t _middleSinceMs = 0;
    uint32_t _bestMs = 0;

    bool _touching = false;
    uint32_t _touchSinceMs = 0;
    uint32_t _lastTouchMs = 0;
    uint32_t _bumps = 0;
};

}  // namespace ball
