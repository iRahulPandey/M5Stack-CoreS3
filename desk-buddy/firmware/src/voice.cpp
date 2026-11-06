#include "voice.h"

#include <M5Unified.h>

using buddy::Mood;

namespace {

using Note = Voice::Note;

constexpr uint32_t NOTE_GAP_MS = 20;

constexpr Note HAPPY[] = {{988, 70}, {1319, 90}};
constexpr Note EXCITED[] = {{1047, 60}, {1319, 60}, {1568, 90}};
constexpr Note DIZZY[] = {{700, 90}, {560, 90}, {700, 90}, {500, 160}};
constexpr Note ANGRY[] = {{196, 160}, {165, 180}, {147, 260}};
constexpr Note SURPRISED[] = {{660, 50}, {1320, 110}};
constexpr Note LOVED[] = {{523, 120}, {659, 120}, {784, 200}};
constexpr Note BORED[] = {{494, 180}, {392, 280}};
constexpr Note CURIOUS[] = {{880, 60}, {1175, 90}};
constexpr Note WAVING[] = {{784, 90}, {1047, 160}, {1319, 90}};  // "hi-i!"

template <size_t N>
constexpr size_t countOf(const Note (&)[N]) {
    return N;
}

}  // namespace

void Voice::play(Mood mood) {
    switch (mood) {
        case Mood::Happy: _notes = HAPPY; _count = countOf(HAPPY); break;
        case Mood::Excited: _notes = EXCITED; _count = countOf(EXCITED); break;
        case Mood::Dizzy: _notes = DIZZY; _count = countOf(DIZZY); break;
        case Mood::Angry: _notes = ANGRY; _count = countOf(ANGRY); break;
        case Mood::Surprised: _notes = SURPRISED; _count = countOf(SURPRISED); break;
        case Mood::Loved: _notes = LOVED; _count = countOf(LOVED); break;
        case Mood::Bored: _notes = BORED; _count = countOf(BORED); break;
        case Mood::Curious: _notes = CURIOUS; _count = countOf(CURIOUS); break;
        case Mood::Waving: _notes = WAVING; _count = countOf(WAVING); break;
        default: _notes = nullptr; _count = 0; break;  // idle and asleep stay quiet
    }
    _next = 0;
    _nextAtMs = 0;
}

void Voice::service(uint32_t nowMs) {
    if (_next >= _count || nowMs < _nextAtMs) return;
    const Note& n = _notes[_next++];
    M5.Speaker.tone(n.hz, n.ms);
    _nextAtMs = nowMs + n.ms + NOTE_GAP_MS;
}
