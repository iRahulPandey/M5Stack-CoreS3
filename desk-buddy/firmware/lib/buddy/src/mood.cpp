#include "mood.h"

#include <string.h>

namespace buddy {

namespace {

constexpr const char* NAMES[] = {"idle", "bored", "asleep", "happy", "excited",
                                 "dizzy", "angry", "surprised", "curious", "loved"};
static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == static_cast<size_t>(Mood::Count), "a name for every mood");

constexpr uint32_t FORCED_REST_MS = 8000;  // how long a forced idle/bored/asleep face is shown

bool interacted(const MoodInputs& in) {
    return in.shake != ShakeLevel::None || in.slap || in.tap || in.poke || in.stroking || in.handNear || in.tilted;
}

}  // namespace

const char* moodName(Mood mood) {
    size_t i = static_cast<size_t>(mood);
    return i < static_cast<size_t>(Mood::Count) ? NAMES[i] : "idle";
}

bool moodFromName(const char* name, Mood& out) {
    for (size_t i = 0; i < static_cast<size_t>(Mood::Count); i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            out = static_cast<Mood>(i);
            return true;
        }
    }
    return false;
}

MoodEngine::MoodEngine(MoodTimings timings) : _t(timings) {}

bool MoodEngine::update(const MoodInputs& in, uint32_t nowMs) {
    decayGrumpiness(nowMs);
    Mood next_ = next(in, nowMs);
    if (interacted(in)) _lastInteractionMs = nowMs;
    if (next_ == _mood) return false;
    _mood = next_;
    _moodSinceMs = nowMs;
    return true;
}

void MoodEngine::force(Mood mood, uint32_t nowMs) {
    _mood = mood;
    _moodSinceMs = nowMs;
    hold(mood, nowMs, durationOf(mood));
}

// Highest priority first: a slap always lands, dizziness runs its course, then the rest.
Mood MoodEngine::next(const MoodInputs& in, uint32_t nowMs) {
    bool hard = in.shake == ShakeLevel::Hard;
    if (hard && !_hardShaking) _hardSinceMs = nowMs;
    _hardShaking = hard;

    if (in.slap) {
        if (_grumpiness < _t.maxGrumpiness) _grumpiness++;
        _lastGrumpChangeMs = nowMs;
        hold(Mood::Angry, nowMs, _t.angryMs + (_grumpiness - 1) * _t.angryExtraPerGrumpMs);
        return Mood::Angry;
    }
    if (holding(Mood::Dizzy, nowMs)) return Mood::Dizzy;
    if (hard && nowMs - _hardSinceMs >= _t.dizzyAfterHardMs) {
        hold(Mood::Dizzy, nowMs, _t.dizzyMs);
        return Mood::Dizzy;
    }
    if (holding(Mood::Angry, nowMs)) return Mood::Angry;

    struct Trigger {
        bool when;
        Mood mood;
        uint32_t forMs;
    };
    const Trigger triggers[] = {
        {in.shake == ShakeLevel::Medium || hard, Mood::Excited, _t.excitedLingerMs},
        {in.shake == ShakeLevel::Gentle, Mood::Happy, _t.happyMs},
        {in.stroking, Mood::Loved, _t.lovedLingerMs},
        {in.poke || in.tap || (_mood == Mood::Asleep && interacted(in)), Mood::Surprised, _t.surprisedMs},
        {in.handNear || in.tilted, Mood::Curious, _t.curiousLingerMs},
    };
    for (const Trigger& t : triggers) {
        if (t.when) {
            hold(t.mood, nowMs, t.forMs);
            return t.mood;
        }
    }
    if (holding(_mood, nowMs)) return _mood;
    if (_mood == Mood::Excited) {  // calm down through happy rather than dropping straight to idle
        hold(Mood::Happy, nowMs, _t.happyMs);
        return Mood::Happy;
    }
    return restingMood(nowMs);
}

Mood MoodEngine::restingMood(uint32_t nowMs) const {
    uint32_t alone = nowMs - _lastInteractionMs;
    if (alone >= _t.asleepAfterMs) return Mood::Asleep;
    if (alone >= _t.boredAfterMs) return Mood::Bored;
    return Mood::Idle;
}

void MoodEngine::hold(Mood mood, uint32_t nowMs, uint32_t forMs) {
    (void)mood;  // the held mood becomes _mood; only the end time is stored
    _holdUntilMs = nowMs + forMs;
}

bool MoodEngine::holding(Mood mood, uint32_t nowMs) const {
    return _mood == mood && static_cast<int32_t>(_holdUntilMs - nowMs) > 0;
}

void MoodEngine::decayGrumpiness(uint32_t nowMs) {
    if (_grumpiness > 0 && nowMs - _lastGrumpChangeMs >= _t.grumpDecayMs) {
        _grumpiness--;
        _lastGrumpChangeMs = nowMs;
    }
}

uint32_t MoodEngine::durationOf(Mood mood) const {
    switch (mood) {
        case Mood::Happy: return _t.happyMs;
        case Mood::Excited: return _t.excitedLingerMs;
        case Mood::Dizzy: return _t.dizzyMs;
        case Mood::Angry: return _t.angryMs;
        case Mood::Surprised: return _t.surprisedMs;
        case Mood::Curious: return _t.curiousLingerMs;
        case Mood::Loved: return _t.lovedLingerMs;
        default: return FORCED_REST_MS;
    }
}

}  // namespace buddy
