#include "motion.h"

#include <math.h>

namespace drama {

namespace {

constexpr float GRAVITY_ALPHA = 0.05f;       // ~200 ms at 100 Hz: follows tilt, ignores shakes
constexpr float ENERGY_ALPHA = 0.03f;        // ~330 ms: smooth enough to grade a shake
constexpr float MIN_GRAVITY_G = 0.1f;
constexpr float REST_ALPHA = 0.0005f;        // ~20 s: a new pose becomes "normal" after a while
constexpr float TILTED_LEAN = 0.42f;         // distance between unit vectors ~25 degrees apart

constexpr float JOLT_ON_G = 0.35f;
constexpr float JOLT_OFF_G = 0.25f;
constexpr uint32_t JOLT_RELEASE_MS = 80;     // dips shorter than this are ringing in the same jolt
constexpr uint32_t IMPACT_MAX_MS = 150;      // a longer jolt is movement, not a hit
constexpr uint32_t QUIET_BEFORE_MS = 600;    // a hit must follow a still moment
constexpr uint32_t QUIET_AFTER_MS = 350;     // ...and be followed by one
// Real slaps on this board peaked at 2.4 to 7.2 g; real taps at 0.5 to 0.8 g (`hits` over serial).
constexpr float SLAP_G = 2.0f;
constexpr uint32_t JUDGE_AFTER_MS = 200;     // long enough to see a shake swing back, short enough to feel instant
constexpr float DROP_SHARE = 0.5f;           // after the spike the signal falls below half of it...
constexpr float REPEAK_SHARE = 0.6f;         // ...and a slap's ringing never climbs back above this share
constexpr uint32_t LANDING_GRACE_MS = 400;

constexpr float FREE_FALL_G = 0.35f;         // total acceleration this low means it is in the air
constexpr uint32_t FREE_FALL_MS = 100;       // 50 ms also caught quick moves of the hand
constexpr float FACE_DOWN_Z = -0.8f;         // gravity's share along the screen's outward axis
constexpr float ON_BACK_Z = 0.8f;
constexpr float STILL_ENERGY = 0.03f;
constexpr uint32_t REST_FOR_PICKUP_MS = 3000;
constexpr float PICKUP_ENERGY = 0.05f;

constexpr float SHAKE_START_ENERGY = 0.15f;
constexpr uint32_t SHAKE_START_MS = 300;
constexpr float SHAKE_STOP_ENERGY = 0.1f;
constexpr uint32_t SHAKE_STOP_MS = 300;
constexpr float MEDIUM_ENERGY = 0.5f;
constexpr float HARD_ENERGY = 1.2f;

ShakeLevel levelFor(float energy) {
    if (energy >= HARD_ENERGY) return ShakeLevel::Hard;
    if (energy >= MEDIUM_ENERGY) return ShakeLevel::Medium;
    return ShakeLevel::Gentle;
}

}  // namespace

const char* hitVerdictName(HitVerdict verdict) {
    switch (verdict) {
        case HitVerdict::Tap: return "tap";
        case HitVerdict::Slap: return "slap";
        case HitVerdict::TooLong: return "too_long";
        case HitVerdict::NotQuietBefore: return "not_quiet_before";
        case HitVerdict::DuringShake: return "during_shake";
        case HitVerdict::FollowedByMotion: return "followed_by_motion";
        case HitVerdict::Landing: return "landing";
    }
    return "tap";
}

void MotionClassifier::update(float ax, float ay, float az, uint32_t nowMs) {
    if (!_started) {
        _gx = _rx = ax;
        _gy = _ry = ay;
        _gz = _rz = az;
        _started = true;
    }
    trackFall(sqrtf(ax * ax + ay * ay + az * az), nowMs);
    if (!_state.falling) {  // in the air there is no "down" to learn
        _gx += GRAVITY_ALPHA * (ax - _gx);
        _gy += GRAVITY_ALPHA * (ay - _gy);
        _gz += GRAVITY_ALPHA * (az - _gz);
    }

    float dx = ax - _gx, dy = ay - _gy, dz = az - _gz;
    float dynamic = sqrtf(dx * dx + dy * dy + dz * dz);
    _state.energy += ENERGY_ALPHA * (dynamic - _state.energy);

    float g = sqrtf(_gx * _gx + _gy * _gy + _gz * _gz);
    if (g > MIN_GRAVITY_G) {
        _state.tiltX = _gx / g;
        _state.tiltY = _gy / g;
        _state.gravityX = _gx / g;
        _state.gravityY = _gy / g;
        _state.gravityZ = _gz / g;
        _state.faceDown = _state.gravityZ < FACE_DOWN_Z;
        _state.onBack = _state.gravityZ > ON_BACK_Z;
        trackLean(g);
    }

    trackJolts(dynamic, nowMs);
    trackShake(nowMs);
    classifyImpact(nowMs);
    trackPickup(nowMs);
}

void MotionClassifier::trackFall(float total, uint32_t nowMs) {
    bool low = total < FREE_FALL_G;
    if (low && !_lowG) _lowGSinceMs = nowMs;
    _lowG = low;
    if (!_state.falling && low && nowMs - _lowGSinceMs >= FREE_FALL_MS) {
        _state.falling = true;
        _state.falls++;
    } else if (_state.falling && !low) {
        _state.falling = false;
        _state.landings++;
        _landed = true;
        _lastLandingMs = nowMs;
    }
}

void MotionClassifier::trackPickup(uint32_t nowMs) {
    bool still = _state.energy < STILL_ENERGY;
    if (still && !_still) _stillSinceMs = nowMs;
    _still = still;
    if (still && nowMs - _stillSinceMs >= REST_FOR_PICKUP_MS) _resting = true;
    if (_resting && _state.energy > PICKUP_ENERGY) {
        _resting = false;
        _state.pickups++;
    }
}

void MotionClassifier::trackLean(float g) {
    float ux = _gx / g, uy = _gy / g, uz = _gz / g;
    _rx += REST_ALPHA * (ux - _rx);
    _ry += REST_ALPHA * (uy - _ry);
    _rz += REST_ALPHA * (uz - _rz);
    float r = sqrtf(_rx * _rx + _ry * _ry + _rz * _rz);
    if (r < MIN_GRAVITY_G) return;
    float lx = ux - _rx / r, ly = uy - _ry / r, lz = uz - _rz / r;
    _state.leanX = lx;
    _state.leanY = ly;
    _state.tilted = sqrtf(lx * lx + ly * ly + lz * lz) > TILTED_LEAN;
}

void MotionClassifier::startJolt(float dynamic, uint32_t nowMs) {
    _inJolt = true;
    _joltStartMs = nowMs;
    _joltPeak = _firstPeak = dynamic;
    _below = false;
    _judged = false;
    _dropped = false;
    _repeaked = false;
    _shakeAtStart = _state.shake != ShakeLevel::None;
    _nearLanding = _state.falling || (_landed && nowMs - _lastLandingMs < LANDING_GRACE_MS);
    dropCandidate();  // a pending hit followed this soon by more movement was not a hit
}

// The first peak of a jolt, whether the signal then fell away, and whether it came back as strong.
void MotionClassifier::trackPeaks(float dynamic) {
    if (dynamic > _joltPeak) _joltPeak = dynamic;
    if (!_dropped) {
        if (dynamic > _firstPeak) {
            _firstPeak = dynamic;
        } else if (dynamic < DROP_SHARE * _firstPeak) {
            _dropped = true;
        }
    } else if (dynamic >= REPEAK_SHARE * _firstPeak) {
        _repeaked = true;  // swinging back as hard: a shake, not a slap
    }
}

// A slap is one sharp spike: strong, never matched again by the ringing after it, not part of a
// shake and not the thud of a landing. It may come while the cube is held or rocking.
void MotionClassifier::judgeHardHit(uint32_t durationMs, uint32_t nowMs) {
    _judged = true;
    // a toss starts the jolt before the fall is confirmed, so look for a landing now as well
    _nearLanding |= _state.falling || (_landed && nowMs - _lastLandingMs < LANDING_GRACE_MS);
    if (!_repeaked && !_shakeAtStart && !_nearLanding) {
        _state.slaps++;
        record(HitVerdict::Slap, _firstPeak, durationMs);
    } else {
        record(_nearLanding ? HitVerdict::Landing : HitVerdict::DuringShake, _firstPeak, durationMs);
    }
}

void MotionClassifier::trackJolts(float dynamic, uint32_t nowMs) {
    if (!_inJolt) {
        if (dynamic > JOLT_ON_G) startJolt(dynamic, nowMs);
        return;
    }
    trackPeaks(dynamic);
    bool hard = _firstPeak >= SLAP_G;
    if (hard && !_judged && nowMs - _joltStartMs >= JUDGE_AFTER_MS) judgeHardHit(JUDGE_AFTER_MS, nowMs);
    if (dynamic >= JOLT_OFF_G) {
        _below = false;
        return;
    }
    if (!_below) {
        _below = true;
        _belowSinceMs = nowMs;
    }
    if (nowMs - _belowSinceMs < JOLT_RELEASE_MS) return;

    _inJolt = false;
    uint32_t durationMs = _belowSinceMs - _joltStartMs;
    bool quietBefore = !_hadJolt || _joltStartMs - _lastJoltEndMs >= QUIET_BEFORE_MS;
    _hadJolt = true;
    _lastJoltEndMs = _belowSinceMs;
    if (_judged) return;
    if (hard) {
        judgeHardHit(durationMs, nowMs);
    } else if (durationMs > IMPACT_MAX_MS) {
        record(HitVerdict::TooLong, _joltPeak, durationMs);
    } else if (!quietBefore) {
        record(HitVerdict::NotQuietBefore, _joltPeak, durationMs);
    } else if (_state.shake != ShakeLevel::None) {
        record(HitVerdict::DuringShake, _joltPeak, durationMs);
    } else {
        _candidate = true;
        _candidateStartMs = _joltStartMs;
        _candidateMs = durationMs;
        _candidatePeak = _joltPeak;
    }
}

void MotionClassifier::record(HitVerdict verdict, float peak, uint32_t durationMs) {
    _state.hits++;
    _state.lastHitVerdict = verdict;
    _state.lastHitPeak = peak;
    _state.lastHitMs = static_cast<uint16_t>(durationMs > 0xFFFF ? 0xFFFF : durationMs);
}

void MotionClassifier::dropCandidate() {
    if (!_candidate) return;
    _candidate = false;
    record(HitVerdict::FollowedByMotion, _candidatePeak, _candidateMs);
}

void MotionClassifier::trackShake(uint32_t nowMs) {
    if (_state.shake == ShakeLevel::None) {
        bool above = _state.energy > SHAKE_START_ENERGY;
        if (above && !_energyAbove) _energyAboveSinceMs = nowMs;
        _energyAbove = above;
        if (above && nowMs - _energyAboveSinceMs >= SHAKE_START_MS) {
            _state.shake = levelFor(_state.energy);
            dropCandidate();
            _quiet = false;
        }
        return;
    }
    if (_state.energy >= SHAKE_STOP_ENERGY) {
        _quiet = false;
        _state.shake = levelFor(_state.energy);
        return;
    }
    if (!_quiet) {
        _quiet = true;
        _quietSinceMs = nowMs;
    }
    if (nowMs - _quietSinceMs >= SHAKE_STOP_MS) {
        _state.shake = ShakeLevel::None;
        _energyAbove = false;
    }
}

void MotionClassifier::classifyImpact(uint32_t nowMs) {
    if (!_candidate || nowMs - _candidateStartMs < QUIET_AFTER_MS) return;
    _candidate = false;  // only gentle jolts get here: hard ones were judged as slaps (or not) already
    if (_state.shake != ShakeLevel::None) {
        record(HitVerdict::DuringShake, _candidatePeak, _candidateMs);
    } else {
        _state.taps++;
        record(HitVerdict::Tap, _candidatePeak, _candidateMs);
    }
}

}  // namespace drama
