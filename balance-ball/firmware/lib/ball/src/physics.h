#pragma once

#include <stdint.h>

#include "level.h"

namespace ball {

struct Arena {
    float width = 320.0f;
    float height = 240.0f;
    float radius = 34.0f;  // the ball's: big enough to read its face on a 2" screen
};

struct Ball {
    float x = 0.0f, y = 0.0f;    // its center, in pixels from the top-left corner
    float vx = 0.0f, vy = 0.0f;  // pixels per second
};

enum Wall : uint8_t { WALL_LEFT = 1, WALL_RIGHT = 2, WALL_TOP = 4, WALL_BOTTOM = 8 };

Ball centered(const Arena& arena);

// Rolls the ball for dtS seconds under `pull` (from Level, in g). The middle sits on top of a gentle
// hill, so a ball left alone slowly drifts off it, and the walls are soft: it bounces back weakly.
Ball roll(const Ball& ball, const Arena& arena, Vec2 pull, float dtS);

uint8_t wallsTouched(const Ball& ball, const Arena& arena);  // a mask of Wall
float fromCenter(const Ball& ball, const Arena& arena);      // pixels
float wallGap(const Ball& ball, const Arena& arena);         // pixels from its edge to the nearest wall

}  // namespace ball
