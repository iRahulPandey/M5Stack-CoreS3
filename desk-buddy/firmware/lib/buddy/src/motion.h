#pragma once

#include <stdint.h>

namespace buddy {

enum class ShakeLevel : uint8_t { None, Gentle, Medium, Hard };

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
    void trackJolts(float dynamic, uint32_t nowMs);
    void trackShake(uint32_t nowMs);
    void classifyImpact(uint32_t nowMs);

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

    bool _candidate = false;  // a short jolt waiting to prove it was isolated
    uint32_t _candidateStartMs = 0;
    float _candidatePeak = 0.0f;

    bool _energyAbove = false;
    uint32_t _energyAboveSinceMs = 0;
    bool _quiet = false;
    uint32_t _quietSinceMs = 0;
};

}  // namespace buddy
