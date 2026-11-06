#pragma once

#include <M5Unified.h>

#include "drama.h"
#include "motion.h"

// Draws the queen's whole face (crown, eyes, brows, mouth and extras like tears or steam)
// for a reaction, animated by time: blinks, rolling eyes, falling tears, a trembling rage.
void drawFace(M5Canvas& canvas, drama::Reaction reaction, uint32_t nowMs, uint32_t sinceMs,
              const drama::MotionState& motion);
