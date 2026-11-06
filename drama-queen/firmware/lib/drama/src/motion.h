#pragma once

#include <stdint.h>

namespace drama {

enum class ShakeLevel : uint8_t { None, Gentle, Medium, Hard };

// Why each jolt did or did not count, for tuning against real slaps.
enum class HitVerdict : uint8_t { Tap, Slap, TooLong, NotQuietBefore, DuringShake, FollowedByMotion, Landing };

const char* hitVerdictName(HitVerdict verdict);

struct MotionState {
    ShakeLevel shake = ShakeLevel::None;
    float energy = 0.0f;  // smoothed movement on top of gravity, in g
    float tiltX = 0.0f;   // gravity direction, -1..1; + when the right side is down
    float tiltY = 0.0f;   // -1..1; + when the bottom edge is down
    float leanX = 0.0f;   // tilt relative to how it usually sits, which it learns over ~20 s
    float leanY = 0.0f;
    bool tilted = false;  // leaned more than ~25 degrees from its usual pose
    uint32_t slaps = 0;   // event counters: a change since the last read means a new event
    uint32_t taps = 0;
    uint32_t hits = 0;    // every judged jolt, with the latest one's details below
    float lastHitPeak = 0.0f;
    uint16_t lastHitMs = 0;
    HitVerdict lastHitVerdict = HitVerdict::Tap;

    float gravityX = 0.0f, gravityY = 0.0f, gravityZ = 1.0f;  // unit vector; z points out of the screen
    bool faceDown = false;   // lying screen-down
    bool onBack = false;     // lying screen-up
    bool falling = false;    // in the air: dropped or tossed
    uint32_t falls = 0;
    uint32_t landings = 0;
    uint32_t pickups = 0;    // started moving after resting for a while
};

// Turns raw accelerometer samples into shake strength, tilt, and one-off slaps and taps.
// A slap or tap is one short, isolated jolt; anything that keeps moving is a shake.
class MotionClassifier {
public:
    // One sample in g, about every 10 ms.
    void update(float ax, float ay, float az, uint32_t nowMs);
    const MotionState& state() const { return _state; }

private:
    void trackLean(float g);
    void trackFall(float total, uint32_t nowMs);
    void trackPickup(uint32_t nowMs);
    void trackJolts(float dynamic, uint32_t nowMs);
    void startJolt(float dynamic, uint32_t nowMs);
    void trackPeaks(float dynamic);
    void judgeHardHit(uint32_t durationMs, uint32_t nowMs);
    void trackShake(uint32_t nowMs);
    void classifyImpact(uint32_t nowMs);
    void record(HitVerdict verdict, float peak, uint32_t durationMs);
    void dropCandidate();

    MotionState _state;
    bool _started = false;
    float _gx = 0.0f, _gy = 0.0f, _gz = 1.0f;  // low-passed gravity
    float _rx = 0.0f, _ry = 0.0f, _rz = 1.0f;  // its usual pose: gravity direction averaged over ~20 s

    bool _inJolt = false;
    uint32_t _joltStartMs = 0;
    float _joltPeak = 0.0f;
    bool _below = false;         // in a jolt, the signal has dropped under the release level...
    uint32_t _belowSinceMs = 0;  // ...since this time
    bool _hadJolt = false;
    uint32_t _lastJoltEndMs = 0;

    float _firstPeak = 0.0f;     // the jolt's first spike, before the signal fell away
    bool _dropped = false;       // the signal fell below half of the first spike...
    bool _repeaked = false;      // ...and then came back nearly as strong (a shake)
    bool _judged = false;        // a hard jolt already counted as a slap, or ruled out
    bool _shakeAtStart = false;
    bool _nearLanding = false;
    bool _landed = false;
    uint32_t _lastLandingMs = 0;

    bool _candidate = false;  // a short jolt waiting to prove it was isolated
    uint32_t _candidateStartMs = 0;
    uint32_t _candidateMs = 0;
    float _candidatePeak = 0.0f;

    bool _energyAbove = false;
    uint32_t _energyAboveSinceMs = 0;
    bool _quiet = false;
    uint32_t _quietSinceMs = 0;

    bool _lowG = false;           // total acceleration near zero...
    uint32_t _lowGSinceMs = 0;    // ...since this time
    bool _resting = false;        // still for long enough that the next movement is a pickup
    uint32_t _stillSinceMs = 0;
    bool _still = false;
};

}  // namespace drama
