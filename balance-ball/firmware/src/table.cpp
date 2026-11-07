#include "table.h"

#include <math.h>
#include <stdio.h>

#include "mood.h"
#include "theme.h"

namespace {

constexpr int GRID_PX = 32;
constexpr int WALL_FLASH_PX = 5;
constexpr int TICK_PX = 6;
constexpr uint32_t SHOW_BEST_FROM_MS = 1000;  // a best this short is not worth bragging about
constexpr int HUD_MARGIN = 8;

void drawRing(M5Canvas& c, int cx, int cy, int r, uint16_t color) {
    c.drawCircle(cx, cy, r, color);
    c.drawCircle(cx, cy, r + 1, color);
    c.drawFastHLine(cx - r - TICK_PX, cy, TICK_PX, color);
    c.drawFastHLine(cx + r + 2, cy, TICK_PX, color);
    c.drawFastVLine(cx, cy - r - TICK_PX, TICK_PX, color);
    c.drawFastVLine(cx, cy + r + 2, TICK_PX, color);
}

void drawWallFlashes(M5Canvas& c, int w, int h, uint8_t walls, uint32_t nowMs) {
    if (walls == 0) return;
    uint16_t color = theme::blend(theme::WALL_HIT, theme::FLOOR, 0.35f + 0.35f * sinf(nowMs * 0.02f));
    if (walls & ball::WALL_LEFT) c.fillRect(0, 0, WALL_FLASH_PX, h, color);
    if (walls & ball::WALL_RIGHT) c.fillRect(w - WALL_FLASH_PX, 0, WALL_FLASH_PX, h, color);
    if (walls & ball::WALL_TOP) c.fillRect(0, 0, w, WALL_FLASH_PX, color);
    if (walls & ball::WALL_BOTTOM) c.fillRect(0, h - WALL_FLASH_PX, w, WALL_FLASH_PX, color);
}

}  // namespace

void drawTable(M5Canvas& c, const ball::Arena& arena, bool home, uint8_t walls, uint32_t nowMs) {
    const int w = arena.width, h = arena.height;
    c.fillScreen(theme::FLOOR);
    for (int x = GRID_PX; x < w; x += GRID_PX) c.drawFastVLine(x, 0, h, theme::GRID);
    for (int y = GRID_PX; y < h; y += GRID_PX) c.drawFastHLine(0, y, w, theme::GRID);
    // the ball is home when it sits wholly inside this ring
    drawRing(c, w / 2, h / 2, arena.radius + ball::MIDDLE_PX, home ? theme::HOME : theme::RING);
    drawWallFlashes(c, w, h, walls, nowMs);
}

void drawHud(M5Canvas& c, uint32_t streakMs, uint32_t bestMs, const char* note) {
    char text[24];
    c.setFont(&fonts::Font2);
    c.setTextColor(theme::TEXT_DIM);
    if (streakMs > 0) {
        c.setTextDatum(top_left);
        snprintf(text, sizeof(text), "%.1f s", streakMs / 1000.0f);
        c.drawString(text, HUD_MARGIN, HUD_MARGIN);
    }
    if (bestMs >= SHOW_BEST_FROM_MS) {
        bool beating = streakMs >= bestMs;
        c.setTextDatum(top_right);
        c.setTextColor(beating ? theme::TEXT_BEST : theme::TEXT_DIM);
        if (beating) {
            c.drawString("new best!", c.width() - HUD_MARGIN, HUD_MARGIN);
        } else {
            snprintf(text, sizeof(text), "best %.1f s", bestMs / 1000.0f);
            c.drawString(text, c.width() - HUD_MARGIN, HUD_MARGIN);
        }
    }
    if (note != nullptr) {
        c.setTextDatum(bottom_center);
        c.setTextColor(theme::TEXT_DIM);
        c.drawString(note, c.width() / 2, c.height() - HUD_MARGIN);
    }
}
