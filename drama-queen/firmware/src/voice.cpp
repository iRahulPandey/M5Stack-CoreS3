#include "voice.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include "synth.h"

using drama::Reaction;
using drama::Synth;
using drama::VoiceStyle;

namespace {

constexpr uint32_t SAMPLE_RATE = 16000;
constexpr uint32_t MAX_CLIP_S = 4;
constexpr int SPEAKER_CHANNEL = 0;

VoiceStyle style(float pitch, float spread, uint16_t syllableMs, uint16_t gapMs, float volume,
                 float vibratoHz = 0.0f, float vibratoDepth = 0.0f) {
    VoiceStyle s;
    s.pitchHz = pitch;
    s.spread = spread;
    s.syllableMs = syllableMs;
    s.gapMs = gapMs;
    s.volume = volume;
    s.vibratoHz = vibratoHz;
    s.vibratoDepth = vibratoDepth;
    return s;
}

VoiceStyle styleFor(Reaction r) {
    switch (r) {
        case Reaction::Outraged:
        case Reaction::Furious:
        case Reaction::Woken: return style(190, 0.15f, 95, 25, 0.75f);
        case Reaction::Wheee:
        case Reaction::Rattled:
        case Reaction::Startled:
        case Reaction::PickedUp: return style(620, 0.30f, 70, 20, 0.65f);
        case Reaction::Sobbing:
        case Reaction::Betrayed: return style(300, 0.10f, 140, 40, 0.6f, 7.0f, 0.06f);
        case Reaction::Asleep:
        case Reaction::Bored: return style(230, 0.10f, 120, 50, 0.4f);
        case Reaction::Smitten:
        case Reaction::Tickled: return style(460, 0.20f, 80, 25, 0.55f);
        case Reaction::Queasy:
        case Reaction::Dizzy:
        case Reaction::Fainted: return style(250, 0.20f, 110, 35, 0.55f, 4.0f, 0.10f);
        default: return style(330, 0.25f, 85, 30, 0.55f);
    }
}

// A sound effect before the words, for the reactions that deserve one.
void addEffect(Synth& s, Reaction r) {
    switch (r) {
        case Reaction::Wheee: s.sweep(500, 1900, 700, 0.6f); break;
        case Reaction::Landed: s.sweep(160, 60, 140, 0.9f); s.silence(150); break;
        case Reaction::Startled: s.sweep(500, 1400, 120, 0.6f); break;
        case Reaction::Outraged: s.sweep(900, 600, 150, 0.6f); break;
        case Reaction::Furious: s.sweep(140, 110, 300, 0.8f); break;
        case Reaction::Fainted: s.sweep(700, 150, 600, 0.5f); s.silence(200); break;
        case Reaction::Dizzy: s.sweep(600, 400, 200, 0.5f); s.sweep(400, 600, 200, 0.5f); break;
        case Reaction::Tickled:
            for (int i = 0; i < 4; i++) {
                s.sweep(900, 1150, 60, 0.5f);
                s.silence(30);
            }
            break;
        case Reaction::Asleep:
            s.sweep(90, 130, 500, 0.4f);
            s.silence(200);
            s.sweep(130, 90, 500, 0.4f);
            return;  // snoring, no words
        default: break;
    }
}

}  // namespace

bool Voice::begin() {
    _capacity = SAMPLE_RATE * MAX_CLIP_S;
    _buffer = static_cast<int16_t*>(heap_caps_malloc(_capacity * sizeof(int16_t), MALLOC_CAP_SPIRAM));
    return _buffer != nullptr;
}

void Voice::say(Reaction reaction, const char* text, uint32_t seed) {
    if (_buffer == nullptr) return;
    M5.Speaker.stop(SPEAKER_CHANNEL);  // the clip buffer is about to be rewritten
    Synth s(_buffer, _capacity, SAMPLE_RATE);
    addEffect(s, reaction);
    if (reaction != Reaction::Asleep) s.babble(text, styleFor(reaction), seed);
    if (s.length() > 0) M5.Speaker.playRaw(s.data(), s.length(), SAMPLE_RATE, false, 1, SPEAKER_CHANNEL, true);
}
