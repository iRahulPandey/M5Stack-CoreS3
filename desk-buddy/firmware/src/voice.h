#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mood.h"

// A few notes per mood on the built-in speaker, played without blocking the animation.
class Voice {
public:
    void play(buddy::Mood mood);
    void service(uint32_t nowMs);

    struct Note {
        uint16_t hz;
        uint16_t ms;
    };

private:
    const Note* _notes = nullptr;
    size_t _count = 0;
    size_t _next = 0;
    uint32_t _nextAtMs = 0;
};
