#include "synth.h"

#include <ctype.h>
#include <math.h>

namespace drama {

namespace {

constexpr float PI_F = 3.14159265f;
constexpr float FULL_SCALE = 32767.0f;
constexpr uint32_t ATTACK_MS = 5;
constexpr uint32_t RELEASE_MS = 20;
constexpr float SQUARE_MIX = 0.6f;    // square for buzz, triangle for body: reads as a tiny voice
constexpr int LETTERS_PER_SYLLABLE = 3;
constexpr float QUESTION_RISE = 1.3f;
constexpr float EXCLAIM_FALL = 0.85f;
constexpr float EXCLAIM_BOOST = 1.2f;
constexpr float SHOUT_BOOST = 1.25f;  // an all-caps word is shouted
constexpr uint32_t ELLIPSIS_PAUSE_MS = 180;

float clamp01(float v) {
    return v > 1.0f ? 1.0f : (v < 0.0f ? 0.0f : v);
}

struct Word {
    int letters = 0;
    int capitals = 0;
    bool question = false;
    bool exclaim = false;
    bool ellipsis = false;
};

// Reads one space-separated word starting at `text`; returns a pointer just past it.
const char* readWord(const char* text, Word& w) {
    w = Word();
    int dots = 0;
    for (; *text && *text != ' '; text++) {
        unsigned char c = static_cast<unsigned char>(*text);
        if (isalpha(c)) {
            w.letters++;
            if (isupper(c)) w.capitals++;
        }
        w.question |= c == '?';
        w.exclaim |= c == '!';
        dots = c == '.' ? dots + 1 : 0;
        w.ellipsis |= dots >= 3;
    }
    return text;
}

}  // namespace

Synth::Synth(int16_t* buffer, size_t capacity, uint32_t sampleRate)
    : _buffer(buffer), _capacity(capacity), _rate(sampleRate) {}

void Synth::clear() {
    _length = 0;
    _phase = 0.0f;
}

void Synth::sweep(float fromHz, float toHz, uint32_t ms, float volume) {
    tone(fromHz, toHz, ms, volume, 0.0f, 0.0f);
}

void Synth::silence(uint32_t ms) {
    size_t n = static_cast<size_t>(ms) * _rate / 1000;
    for (size_t i = 0; i < n && _length < _capacity; i++) _buffer[_length++] = 0;
}

void Synth::babble(const char* text, const VoiceStyle& style, uint32_t seed) {
    _random = seed ? seed : 1;
    while (*text) {
        while (*text == ' ') text++;
        Word w;
        text = readWord(text, w);
        if (w.letters == 0) continue;
        int syllables = (w.letters + LETTERS_PER_SYLLABLE - 1) / LETTERS_PER_SYLLABLE;
        bool shout = w.letters > 1 && w.capitals * 2 > w.letters;
        for (int i = 0; i < syllables; i++) {
            bool last = i == syllables - 1;
            float wander = (static_cast<float>(nextRandom() >> 8) / 16777216.0f) * 2.0f - 1.0f;
            float start = style.pitchHz * (1.0f + style.spread * wander);
            float end = start * (last && w.question ? QUESTION_RISE : (last && w.exclaim ? EXCLAIM_FALL : 0.97f));
            float volume = style.volume * (shout ? SHOUT_BOOST : 1.0f) * (last && w.exclaim ? EXCLAIM_BOOST : 1.0f);
            uint32_t ms = style.syllableMs * (85 + nextRandom() % 31) / 100;  // +-15%
            tone(start, end, ms, clamp01(volume), style.vibratoHz, style.vibratoDepth);
            silence(style.gapMs);
        }
        silence(w.ellipsis ? ELLIPSIS_PAUSE_MS : style.gapMs);
    }
}

void Synth::tone(float fromHz, float toHz, uint32_t ms, float volume, float vibratoHz, float vibratoDepth) {
    size_t n = static_cast<size_t>(ms) * _rate / 1000;
    size_t attack = _rate * ATTACK_MS / 1000, release = _rate * RELEASE_MS / 1000;
    if (attack + release > n) attack = release = n / 2;
    for (size_t i = 0; i < n && _length < _capacity; i++) {
        float progress = static_cast<float>(i) / n;
        float hz = fromHz + (toHz - fromHz) * progress;
        if (vibratoDepth > 0.0f) hz *= 1.0f + vibratoDepth * sinf(2.0f * PI_F * vibratoHz * i / _rate);
        _phase += hz / _rate;
        _phase -= floorf(_phase);
        float square = _phase < 0.5f ? 1.0f : -1.0f;
        float triangle = 4.0f * fabsf(_phase - 0.5f) - 1.0f;
        float envelope = i < attack ? static_cast<float>(i) / attack
                                    : (i >= n - release ? static_cast<float>(n - i) / release : 1.0f);
        float sample = (SQUARE_MIX * square + (1.0f - SQUARE_MIX) * triangle) * envelope * volume;
        _buffer[_length++] = static_cast<int16_t>(sample * FULL_SCALE);
    }
}

uint32_t Synth::nextRandom() {
    _random = _random * 1664525u + 1013904223u;
    return _random;
}

}  // namespace drama
