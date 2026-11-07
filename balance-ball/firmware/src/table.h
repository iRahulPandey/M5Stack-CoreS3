#pragma once

#include <M5Unified.h>

#include "physics.h"

// The table the ball rolls on: a faint grid, a ring marking the middle (green once the ball is home),
// and a red flash along any wall the ball is pressed against.
void drawTable(M5Canvas& canvas, const ball::Arena& arena, bool home, uint8_t walls, uint32_t nowMs);

// How long it has stayed in the middle, its best, and a short note at the bottom (or nullptr).
void drawHud(M5Canvas& canvas, uint32_t streakMs, uint32_t bestMs, const char* note);
