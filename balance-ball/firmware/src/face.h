#pragma once

#include <M5Unified.h>

#include "mood.h"

struct BallView {
    float x = 0.0f, y = 0.0f;          // its center on screen
    float radius = 34.0f;
    uint8_t walls = 0;                 // ball::Wall mask: it squashes flat against these
    float lookX = 0.0f, lookY = 0.0f;  // where it would rather be, each -1..1
    ball::Mood mood = ball::Mood::Happy;
    uint32_t moodForMs = 0;            // how long it has felt this way
    float redness = 0.0f;              // 0 its usual yellow, 1 angry red
};

void drawBall(M5Canvas& canvas, const BallView& view, uint32_t nowMs);
