#pragma once

#include <M5Unified.h>

#include "wall.h"

// What the card at the bottom says, beyond what the wall itself knows.
struct Caption {
    const char* reply = nullptr;  // what the wall said to the last touch, while it still shows
    bool signShouting = false;    // the sign turns red and says DON'T! for a moment after a fingerprint
};

// The wall fills the screen: its colour deepens and shines while wet, roller marks fade as it dries,
// drips and fingerprints stay where they are. A WET PAINT sign stands until it is dry, and a card at
// the bottom reports progress with appropriate solemnity.
void drawWall(M5Canvas& canvas, const paint::Wall& wall, const Caption& caption, uint32_t nowMs);
