#include "drama.h"

#include <string.h>

#include <initializer_list>

namespace drama {

namespace {

constexpr const char* NAMES[] = {"idle",     "bored",   "asleep",   "woken",    "picked_up", "wheee",
                                 "landed",   "queasy",  "fainted",  "sobbing",  "betrayed",  "rattled",
                                 "dizzy",    "outraged", "furious", "startled", "tickled",   "smitten"};
static_assert(sizeof(NAMES) / sizeof(NAMES[0]) == static_cast<size_t>(Reaction::Count), "a name for every reaction");

constexpr uint8_t MAX_SLAP_STREAK = 9;

bool interacted(const Inputs& in) {
    return in.shake != ShakeLevel::None || in.slap || in.tap || in.poke || in.pickedUp || in.landed ||
           in.stroking || in.falling || in.faceDown || in.onBack || in.tilted;
}

}  // namespace

const char* reactionName(Reaction reaction) {
    size_t i = static_cast<size_t>(reaction);
    return i < static_cast<size_t>(Reaction::Count) ? NAMES[i] : "idle";
}

bool reactionFromName(const char* name, Reaction& out) {
    for (size_t i = 0; i < static_cast<size_t>(Reaction::Count); i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            out = static_cast<Reaction>(i);
            return true;
        }
    }
    return false;
}

DramaEngine::DramaEngine(Timings timings) : _t(timings) {}

bool DramaEngine::update(const Inputs& in, uint32_t nowMs) {
    Reaction r = next(in, nowMs);
    if (interacted(in)) _lastInteractionMs = nowMs;
    if (r == _reaction) return false;
    _reaction = r;
    _sinceMs = nowMs;
    return true;
}

void DramaEngine::force(Reaction reaction, uint32_t nowMs) {
    _reaction = reaction;
    _sinceMs = nowMs;
    _forced = true;
    uint32_t usual = durationOf(reaction);
    hold(reaction, nowMs, usual > _t.forcedRestMs ? usual : _t.forcedRestMs);  // long enough to film
}

// Highest priority first: being in the air beats everything, then a slap, then dizziness.
Reaction DramaEngine::next(const Inputs& in, uint32_t nowMs) {
    trackSince(in.shake == ShakeLevel::Hard, _hard, _hardSinceMs, nowMs);
    trackSince(in.faceDown, _faceDown, _faceDownSinceMs, nowMs);
    trackSince(in.onBack, _onBack, _onBackSinceMs, nowMs);
    trackSince(in.tilted, _tilted, _tiltedSinceMs, nowMs);

    if (_forced) {  // shown on request, e.g. while filming with the cube in hand: runs its full time
        if (holding(_reaction, nowMs)) return _reaction;
        _forced = false;
    }
    if (in.falling) return Reaction::Wheee;
    if (in.landed && _reaction == Reaction::Wheee) return hold(Reaction::Landed, nowMs, _t.landedMs);
    if (in.slap) {
        bool recent = _slapStreak > 0 && nowMs - _lastSlapMs < _t.furyWindowMs;
        _slapStreak = recent ? (_slapStreak < MAX_SLAP_STREAK ? _slapStreak + 1 : MAX_SLAP_STREAK) : 1;
        _lastSlapMs = nowMs;
        return _slapStreak >= 2 ? hold(Reaction::Furious, nowMs, _t.furiousMs)
                                : hold(Reaction::Outraged, nowMs, _t.outragedMs);
    }
    if (holding(Reaction::Dizzy, nowMs)) return Reaction::Dizzy;
    if (_hard && nowMs - _hardSinceMs >= _t.dizzyAfterHardMs) return hold(Reaction::Dizzy, nowMs, _t.dizzyMs);
    for (Reaction r : {Reaction::Outraged, Reaction::Furious, Reaction::Landed}) {
        if (holding(r, nowMs)) return r;
    }
    Reaction p = posture(in, nowMs);
    if (p != Reaction::Count) return p;

    if (in.shake != ShakeLevel::None) return hold(Reaction::Rattled, nowMs, _t.rattledLingerMs);
    if (_tilted && nowMs - _tiltedSinceMs >= _t.queasyAfterMs) return Reaction::Queasy;
    if (in.stroking) return hold(Reaction::Smitten, nowMs, _t.smittenLingerMs);
    if (_reaction == Reaction::Asleep && interacted(in)) return hold(Reaction::Woken, nowMs, _t.wokenMs);
    if (in.poke) return hold(Reaction::Tickled, nowMs, _t.tickledMs);
    if (in.tap) return hold(Reaction::Startled, nowMs, _t.startledMs);
    if (in.pickedUp) return hold(Reaction::PickedUp, nowMs, _t.pickedUpMs);
    if (holding(_reaction, nowMs)) return _reaction;
    return resting(nowMs);
}

// How she is lying: face down she sobs, turned back up she feels betrayed, on her back she faints.
// Returns Count when posture has nothing to say.
Reaction DramaEngine::posture(const Inputs& in, uint32_t nowMs) {
    (void)in;
    if (_faceDown && nowMs - _faceDownSinceMs >= _t.sobAfterMs) return Reaction::Sobbing;
    if (_reaction == Reaction::Sobbing && !_faceDown) return hold(Reaction::Betrayed, nowMs, _t.betrayedMs);
    if (holding(Reaction::Betrayed, nowMs)) return Reaction::Betrayed;
    if (_onBack && nowMs - _onBackSinceMs >= _t.faintAfterMs) return Reaction::Fainted;
    return Reaction::Count;
}

Reaction DramaEngine::resting(uint32_t nowMs) const {
    uint32_t alone = nowMs - _lastInteractionMs;
    if (alone >= _t.asleepAfterMs) return Reaction::Asleep;
    if (alone >= _t.boredAfterMs) return Reaction::Bored;
    return Reaction::Idle;
}

Reaction DramaEngine::hold(Reaction reaction, uint32_t nowMs, uint32_t forMs) {
    _holdUntilMs = nowMs + forMs;
    return reaction;
}

bool DramaEngine::holding(Reaction reaction, uint32_t nowMs) const {
    return _reaction == reaction && static_cast<int32_t>(_holdUntilMs - nowMs) > 0;
}

void DramaEngine::trackSince(bool active, bool& was, uint32_t& since, uint32_t nowMs) {
    if (active && !was) since = nowMs;
    was = active;
}

uint32_t DramaEngine::durationOf(Reaction reaction) const {
    switch (reaction) {
        case Reaction::Woken: return _t.wokenMs;
        case Reaction::PickedUp: return _t.pickedUpMs;
        case Reaction::Landed: return _t.landedMs;
        case Reaction::Betrayed: return _t.betrayedMs;
        case Reaction::Rattled: return _t.rattledLingerMs;
        case Reaction::Dizzy: return _t.dizzyMs;
        case Reaction::Outraged: return _t.outragedMs;
        case Reaction::Furious: return _t.furiousMs;
        case Reaction::Startled: return _t.startledMs;
        case Reaction::Tickled: return _t.tickledMs;
        case Reaction::Smitten: return _t.smittenLingerMs;
        default: return _t.forcedRestMs;
    }
}

}  // namespace drama
