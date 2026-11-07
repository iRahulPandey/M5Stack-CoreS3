#pragma once

#include <M5Unified.h>

#include "world.h"

constexpr int CELL_PX = 2;      // each cell is 2 x 2 pixels
constexpr int TOOLBAR_PX = 28;  // the toolbar's height; the box fills the rest of the screen

enum class Tool : uint8_t { Sand, Water, Wall, Erase };
constexpr int TOOL_BUTTONS = 5;  // the four tools, then "clear"
constexpr int CLEAR_BUTTON = 4;

// Writes every cell (World::cells() layout, row by row) straight into the canvas's frame buffer below
// the toolbar: 17,000 cells would take far longer one fillRect at a time. Water shimmers with `nowMs`.
void drawWorld(M5Canvas& canvas, const uint8_t* cells, int width, int height, uint32_t nowMs);

void drawToolbar(M5Canvas& canvas, Tool selected);
int toolbarButtonAt(int x, int screenWidth);  // 0..4 across the toolbar
const char* toolName(Tool tool);
