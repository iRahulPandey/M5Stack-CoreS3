#pragma once

#include <stddef.h>
#include <stdint.h>

#include "drama.h"

// Her "voice": a sound effect for the reaction followed by the line in gibberish, built as one clip
// in PSRAM and played on the speaker without blocking the animation.
class Voice {
public:
    bool begin();
    void say(drama::Reaction reaction, const char* text, uint32_t seed);

private:
    int16_t* _buffer = nullptr;
    size_t _capacity = 0;
};
