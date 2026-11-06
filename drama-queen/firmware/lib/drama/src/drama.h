#pragma once

#include <stdint.h>

#include "motion.h"

namespace drama {

enum class Reaction : uint8_t {
    Idle,      // smug, the odd remark about itself
    Bored,     // nobody has touched it for a minute
    Asleep,    // beauty sleep
    Woken,     // "I WAS SLEEPING!"
    PickedUp,  // "Careful! I'm fragile!"
    Wheee,     // in the air
    Landed,    // "...nailed it."
    Queasy,    // held tilted
    Fainted,   // lying on its back
    Sobbing,   // lying face down
    Betrayed,  // turned back up after sobbing
    Rattled,   // being shaken
    Dizzy,     // shaken hard for a while
    Outraged,  // slapped
    Furious,   // slapped again
    Startled,  // bumped
    Tickled,   // screen tapped
    Smitten,   // screen stroked
    Count
};

const char* reactionName(Reaction reaction);
bool reactionFromName(const char* name, Reaction& out);

struct Inputs {
    ShakeLevel shake = ShakeLevel::None;
    bool slap = false;      // new events since the last update...
    bool tap = false;
    bool poke = false;
    bool pickedUp = false;
    bool landed = false;
    bool stroking = false;  // ...and things that are true right now
    bool falling = false;
    bool faceDown = false;
    bool onBack = false;
    bool tilted = false;
};

struct Timings {
    uint32_t boredAfterMs = 60000;
    uint32_t asleepAfterMs = 180000;
    uint32_t dizzyAfterHardMs = 1000;
    uint32_t dizzyMs = 4000;
    uint32_t outragedMs = 4000;
    uint32_t furiousMs = 6000;
    uint32_t furyWindowMs = 15000;  // a second slap within this long makes it furious
    uint32_t landedMs = 2500;
    uint32_t betrayedMs = 3000;
    uint32_t sobAfterMs = 600;      // lying face down this long before the tears start
    uint32_t faintAfterMs = 800;
    uint32_t queasyAfterMs = 1500;
    uint32_t rattledLingerMs = 800;
    uint32_t smittenLingerMs = 2000;
    uint32_t startledMs = 1500;
    uint32_t tickledMs = 2000;
    uint32_t pickedUpMs = 2500;
    uint32_t wokenMs = 3000;
    uint32_t forcedRestMs = 8000;   // how long a forced idle/bored/asleep face is shown
};

// Picks the one reaction the queen is having, from what is being done to her and how long she was left alone.
class DramaEngine {
public:
    explicit DramaEngine(Timings timings = Timings());

    // Returns true when the reaction changed on this update.
    bool update(const Inputs& in, uint32_t nowMs);
    // Shows a reaction on request (demos, screenshots), for as long as it would normally last.
    void force(Reaction reaction, uint32_t nowMs);

    Reaction reaction() const { return _reaction; }
    uint32_t sinceMs() const { return _sinceMs; }
    uint8_t slapStreak() const { return _slapStreak; }

private:
    Reaction next(const Inputs& in, uint32_t nowMs);
    Reaction posture(const Inputs& in, uint32_t nowMs);
    Reaction resting(uint32_t nowMs) const;
    Reaction hold(Reaction reaction, uint32_t nowMs, uint32_t forMs);
    bool holding(Reaction reaction, uint32_t nowMs) const;
    uint32_t durationOf(Reaction reaction) const;
    void trackSince(bool active, bool& was, uint32_t& since, uint32_t nowMs);

    Timings _t;
    Reaction _reaction = Reaction::Idle;
    uint32_t _sinceMs = 0;
    uint32_t _holdUntilMs = 0;
    uint32_t _lastInteractionMs = 0;
    uint32_t _lastSlapMs = 0;
    uint8_t _slapStreak = 0;
    bool _forced = false;  // shown on request: holds for its full time, whatever happens
    bool _hard = false, _faceDown = false, _onBack = false, _tilted = false;
    uint32_t _hardSinceMs = 0, _faceDownSinceMs = 0, _onBackSinceMs = 0, _tiltedSinceMs = 0;
};

}  // namespace drama
