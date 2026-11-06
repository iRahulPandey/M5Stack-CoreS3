#include "motion.h"

#include <math.h>

namespace buddy {

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
constexpr float SLAP_G = 1.8f;

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
    _gx += GRAVITY_ALPHA * (ax - _gx);
    _gy += GRAVITY_ALPHA * (ay - _gy);
    _gz += GRAVITY_ALPHA * (az - _gz);

    float dx = ax - _gx, dy = ay - _gy, dz = az - _gz;
    float dynamic = sqrtf(dx * dx + dy * dy + dz * dz);
    _state.energy += ENERGY_ALPHA * (dynamic - _state.energy);

    float g = sqrtf(_gx * _gx + _gy * _gy + _gz * _gz);
    if (g > MIN_GRAVITY_G) {
        _state.tiltX = _gx / g;
        _state.tiltY = _gy / g;
        trackLean(g);
    }

    trackJolts(dynamic, nowMs);
    trackShake(nowMs);
    classifyImpact(nowMs);
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

void MotionClassifier::trackJolts(float dynamic, uint32_t nowMs) {
    if (!_inJolt) {
        if (dynamic > JOLT_ON_G) {
            _inJolt = true;
            _joltStartMs = nowMs;
            _joltPeak = dynamic;
            _below = false;
            dropCandidate();  // a pending hit followed this soon by more movement was not a hit
        }
        return;
    }
    if (dynamic > _joltPeak) _joltPeak = dynamic;
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
    if (durationMs > IMPACT_MAX_MS) {
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
    _candidate = false;
    if (_state.shake != ShakeLevel::None) {
        record(HitVerdict::DuringShake, _candidatePeak, _candidateMs);
    } else if (_candidatePeak >= SLAP_G) {
        _state.slaps++;
        record(HitVerdict::Slap, _candidatePeak, _candidateMs);
    } else {
        _state.taps++;
        record(HitVerdict::Tap, _candidatePeak, _candidateMs);
    }
}

}  // namespace buddy
