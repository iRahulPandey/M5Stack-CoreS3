#pragma once

#include <stdint.h>

#include "motion.h"

namespace buddy {

enum class Mood : uint8_t { Idle, Bored, Asleep, Happy, Excited, Dizzy, Angry, Surprised, Curious, Loved, Count };

const char* moodName(Mood mood);
bool moodFromName(const char* name, Mood& out);

struct MoodInputs {
    ShakeLevel shake = ShakeLevel::None;
    bool slap = false;      // a new slap since the last update
    bool tap = false;       // a new bump since the last update
    bool poke = false;      // the screen was tapped
    bool stroking = false;  // a finger is sliding on the screen
    bool handNear = false;  // the proximity sensor sees something close
    bool tilted = false;    // held well away from upright
};

struct MoodTimings {
    uint32_t boredAfterMs = 120000;
    uint32_t asleepAfterMs = 420000;
    uint32_t dizzyAfterHardMs = 1200;   // hard shaking this long makes it dizzy
    uint32_t dizzyMs = 4000;
    uint32_t angryMs = 5000;
    uint32_t angryExtraPerGrumpMs = 4000;
    uint32_t grumpDecayMs = 20000;      // one level of grumpiness fades per this long without a slap
    uint8_t maxGrumpiness = 4;
    uint32_t happyMs = 2500;
    uint32_t excitedLingerMs = 1500;
    uint32_t surprisedMs = 1200;
    uint32_t lovedLingerMs = 2000;
    uint32_t curiousLingerMs = 1000;
};

// Decides how the buddy feels from what is happening to it and how long it has been left alone.
class MoodEngine {
public:
    explicit MoodEngine(MoodTimings timings = MoodTimings());

    // Returns true when the mood changed on this update.
    bool update(const MoodInputs& in, uint32_t nowMs);
    // Shows a mood on request (demos, screenshots), for as long as it would normally last.
    void force(Mood mood, uint32_t nowMs);

    Mood mood() const { return _mood; }
    uint32_t moodSinceMs() const { return _moodSinceMs; }
    uint8_t grumpiness() const { return _grumpiness; }

private:
    Mood next(const MoodInputs& in, uint32_t nowMs);
    Mood restingMood(uint32_t nowMs) const;
    void hold(Mood mood, uint32_t nowMs, uint32_t forMs);
    bool holding(Mood mood, uint32_t nowMs) const;
    void decayGrumpiness(uint32_t nowMs);
    uint32_t durationOf(Mood mood) const;

    MoodTimings _t;
    Mood _mood = Mood::Idle;
    uint32_t _moodSinceMs = 0;
    uint32_t _holdUntilMs = 0;
    uint32_t _lastInteractionMs = 0;
    uint32_t _hardSinceMs = 0;
    bool _hardShaking = false;
    uint8_t _grumpiness = 0;
    uint32_t _lastGrumpChangeMs = 0;
};

}  // namespace buddy
