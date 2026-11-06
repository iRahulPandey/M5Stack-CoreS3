#pragma once

#include <M5Unified.h>

#include "sessionState.h"

enum class Page : uint8_t { Tokens, Model, Context, Now, Cost, Count };

Page nextPage(Page page);

// Draws one full page into the canvas; the caller pushes it to the display.
void drawPage(M5Canvas& canvas, Page page, const SessionState& state, bool linkUp, uint32_t nowMs);
