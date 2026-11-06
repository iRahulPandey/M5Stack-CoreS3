#pragma once

#include <M5Unified.h>

// A comic speech bubble along the bottom that types out the current line, letter by letter.
class SpeechBubble {
public:
    void say(const char* text, uint32_t nowMs);
    void draw(M5Canvas& canvas, uint32_t nowMs, float jitter) const;
    const char* text() const { return _text; }

private:
    static constexpr size_t MAX_TEXT = 64;
    char _text[MAX_TEXT] = "";
    uint32_t _startMs = 0;
};
