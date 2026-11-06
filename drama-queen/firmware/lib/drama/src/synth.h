#pragma once

#include <stddef.h>
#include <stdint.h>

namespace drama {

// How a reaction sounds when it "talks": gibberish syllables, one per ~3 letters of the line.
struct VoiceStyle {
    float pitchHz = 330.0f;     // middle of the voice
    float spread = 0.25f;       // how far each syllable wanders from it, as a share
    uint16_t syllableMs = 85;
    uint16_t gapMs = 30;
    float vibratoHz = 0.0f;     // wobble (sobbing, queasy)
    float vibratoDepth = 0.0f;
    float volume = 0.55f;       // 0..1
};

// Short sound clips written into a 16-bit mono buffer. Each returns the number of samples written
// and never writes past `capacity`.
class Synth {
public:
    Synth(int16_t* buffer, size_t capacity, uint32_t sampleRate);

    void clear();
    // A glide from one pitch to another: screams, gasps, thuds.
    void sweep(float fromHz, float toHz, uint32_t ms, float volume);
    void silence(uint32_t ms);
    // The line in gibberish. `seed` makes the same line sound the same each time.
    void babble(const char* text, const VoiceStyle& style, uint32_t seed);

    size_t length() const { return _length; }
    const int16_t* data() const { return _buffer; }

private:
    void tone(float fromHz, float toHz, uint32_t ms, float volume, float vibratoHz, float vibratoDepth);
    uint32_t nextRandom();

    int16_t* _buffer;
    size_t _capacity;
    uint32_t _rate;
    size_t _length = 0;
    float _phase = 0.0f;
    uint32_t _random = 1;
};

}  // namespace drama
