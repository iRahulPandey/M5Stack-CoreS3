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
constexpr uint32_t JUDGE_AFTER_MS = 300;     // a 2 Hz shake swings back within 250 ms; still feels instant
constexpr float DROP_SHARE = 0.5f;           // after the spike the signal falls below half of it...
constexpr float REPEAK_SHARE = 0.6f;         // ...and a slap's ringing never climbs back above this share
constexpr uint32_t LOOK_BACK_MS = 300;       // nor came anything as strong just before (a shake's last swing)
constexpr float FOOT_G = 1.0f;               // where a spike's rise begins, looking back from its peak
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

    trackSpike(dynamic, nowMs);
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

// A slap is one sharp spike of SLAP_G or more, watched on its own whatever else is moving (the cube may
// be held or rocking). It stands alone on both sides: nothing in the LOOK_BACK_MS before its rise and
// nothing in the JUDGE_AFTER_MS after its fall comes within REPEAK_SHARE of it. A shake's swings have
// a neighbour as hard, a push never falls away, and a landing comes after a fall.
void MotionClassifier::trackSpike(float dynamic, uint32_t nowMs) {
    if (_spike) {
        watchSpike(dynamic, nowMs);
    } else if (dynamic >= SLAP_G) {
        _spike = true;
        _spikeStartMs = nowMs;
        _spikePeak = dynamic;
        _spikeBefore = strongestBefore(nowMs);
        _spikeDropped = _spikeRepeaked = false;
    }
    remember(dynamic, nowMs);
}

void MotionClassifier::watchSpike(float dynamic, uint32_t nowMs) {
    if (!_spikeDropped) {
        if (dynamic > _spikePeak) {
            _spikePeak = dynamic;
        } else if (dynamic < DROP_SHARE * _spikePeak) {
            _spikeDropped = true;
        }
    } else if (dynamic >= REPEAK_SHARE * _spikePeak) {
        _spikeRepeaked = true;
    }
    if (nowMs - _spikeStartMs < JUDGE_AFTER_MS) return;

    _spike = false;
    bool nearLanding = _state.falling || (_landed && nowMs - _lastLandingMs < LANDING_GRACE_MS);
    if (nearLanding) {
        record(HitVerdict::Landing, _spikePeak, JUDGE_AFTER_MS);
    } else if (_spikeRepeaked) {
        record(HitVerdict::DuringShake, _spikePeak, JUDGE_AFTER_MS);
    } else if (_spikeBefore >= REPEAK_SHARE * _spikePeak) {
        record(HitVerdict::NotQuietBefore, _spikePeak, JUDGE_AFTER_MS);
    } else if (!_spikeDropped) {
        record(HitVerdict::TooLong, _spikePeak, JUDGE_AFTER_MS);
    } else {
        _state.slaps++;
        record(HitVerdict::Slap, _spikePeak, JUDGE_AFTER_MS);
    }
}

// The strongest movement in the LOOK_BACK_MS before a spike, leaving out the spike's own rise: walk
// back to where the signal was last under FOOT_G, then take the largest sample before that.
float MotionClassifier::strongestBefore(uint32_t nowMs) const {
    float strongest = 0.0f;
    bool pastRise = false;
    for (int i = 1; i <= _historyCount; i++) {
        int k = (_historyNext - i + HISTORY) % HISTORY;
        if (nowMs - _historyMs[k] > LOOK_BACK_MS) break;
        if (!pastRise) {
            pastRise = _history[k] < FOOT_G;
        } else if (_history[k] > strongest) {
            strongest = _history[k];
        }
    }
    return pastRise ? strongest : INFINITY;  // above FOOT_G the whole time: never calm enough
}

void MotionClassifier::remember(float dynamic, uint32_t nowMs) {
    _history[_historyNext] = dynamic;
    _historyMs[_historyNext] = nowMs;
    _historyNext = (_historyNext + 1) % HISTORY;
    if (_historyCount < HISTORY) _historyCount++;
}

// Jolts are for taps: gentle, short, isolated knocks. Anything that reached SLAP_G is the spike
// tracker's to judge, so it is never a tap.
void MotionClassifier::trackJolts(float dynamic, uint32_t nowMs) {
    if (!_inJolt) {
        if (dynamic > JOLT_ON_G) {
            _inJolt = true;
            _joltStartMs = nowMs;
            _joltPeak = dynamic;
            _below = false;
            dropCandidate();  // a pending tap followed this soon by more movement was not a tap
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
    if (_joltPeak >= SLAP_G) return;  // judged by the spike tracker
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
    _candidate = false;  // only gentle jolts get here: hard ones were judged as slaps (or not) already
    if (_state.shake != ShakeLevel::None) {
        record(HitVerdict::DuringShake, _candidatePeak, _candidateMs);
    } else {
        _state.taps++;
        record(HitVerdict::Tap, _candidatePeak, _candidateMs);
    }
}

}  // namespace drama
