#include "view.h"

#include "theme.h"

using sand::Material;

namespace {

constexpr int SHADES = 32;
constexpr uint32_t SHIMMER_MS = 120;  // water's shades drift one step this often
constexpr int BUTTON_GAP = 3;
constexpr int SWATCH_R = 5;

const char* const LABELS[TOOL_BUTTONS] = {"sand", "water", "wall", "erase", "clear"};

// The sprite stores pixels big-endian; the ESP32 is little-endian.
uint16_t pixel(theme::Rgb c) {
    return __builtin_bswap16(rgb565(c.r, c.g, c.b));
}

theme::Rgb mix(theme::Rgb a, theme::Rgb b, int step, int steps) {
    auto m = [step, steps](uint8_t x, uint8_t y) { return static_cast<uint8_t>(x + (y - x) * step / steps); };
    return theme::Rgb{m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}

// Ready-to-write pixels: a ramp of shades for each material, and one sky colour per cell row.
struct Palette {
    uint16_t sand[SHADES], water[SHADES], wall[SHADES];
    uint16_t sky[240 / CELL_PX];
    bool ready = false;

    void build(int rows) {
        for (int i = 0; i < SHADES; i++) {
            sand[i] = pixel(mix(theme::SAND_DARK, theme::SAND_LIGHT, i, SHADES - 1));
            wall[i] = pixel(mix(theme::WALL_DARK, theme::WALL_LIGHT, i, SHADES - 1));
            int wave = i < SHADES / 2 ? i * 2 : (SHADES - 1 - i) * 2;  // up and back down: a shimmer loops
            water[i] = pixel(mix(theme::WATER_DARK, theme::WATER_LIGHT, wave, SHADES - 1));
        }
        for (int y = 0; y < rows; y++) sky[y] = pixel(mix(theme::SKY_TOP, theme::SKY_BOTTOM, y, rows - 1));
        ready = true;
    }
};

Palette palette;

uint16_t colorOf(uint8_t cell, int x, int y, int shimmer) {
    uint8_t shade = sand::World::shadeOf(cell);
    switch (sand::World::materialOf(cell)) {
        case Material::Sand: return palette.sand[shade];
        case Material::Water: return palette.water[(shade + shimmer + x / 3) % SHADES];
        case Material::Wall: return palette.wall[shade];
        case Material::Empty: break;
    }
    return palette.sky[y];
}

uint16_t swatchOf(int button) {
    switch (button) {
        case 0: return rgb565(theme::SAND_LIGHT.r, theme::SAND_LIGHT.g, theme::SAND_LIGHT.b);
        case 1: return rgb565(theme::WATER_LIGHT.r, theme::WATER_LIGHT.g, theme::WATER_LIGHT.b);
        case 2: return rgb565(theme::WALL_LIGHT.r, theme::WALL_LIGHT.g, theme::WALL_LIGHT.b);
        default: return theme::ERASE;
    }
}

}  // namespace

void drawWorld(M5Canvas& canvas, const uint8_t* cells, int width, int height, uint32_t nowMs) {
    if (!palette.ready) palette.build(height);
    const int stride = canvas.width();
    uint16_t* frame = static_cast<uint16_t*>(canvas.getBuffer()) + TOOLBAR_PX * stride;
    const int shimmer = static_cast<int>(nowMs / SHIMMER_MS % SHADES);
    for (int y = 0; y < height; y++) {
        uint16_t* top = frame + y * CELL_PX * stride;
        uint16_t* bottom = top + stride;
        for (int x = 0; x < width; x++) {
            uint16_t c = colorOf(*cells++, x, y, shimmer);
            top[2 * x] = top[2 * x + 1] = bottom[2 * x] = bottom[2 * x + 1] = c;
        }
    }
}

void drawToolbar(M5Canvas& c, Tool selected) {
    const int w = c.width() / TOOL_BUTTONS;
    c.fillRect(0, 0, c.width(), TOOLBAR_PX, theme::BAR);
    c.setFont(&fonts::Font2);
    c.setTextDatum(middle_left);
    for (int i = 0; i < TOOL_BUTTONS; i++) {
        bool on = i == static_cast<int>(selected);
        int x = i * w + BUTTON_GAP, bw = w - 2 * BUTTON_GAP, cy = TOOLBAR_PX / 2;
        c.fillRoundRect(x, BUTTON_GAP, bw, TOOLBAR_PX - 2 * BUTTON_GAP, 6, on ? theme::BUTTON_ON : theme::BUTTON);
        int textX = x + 8;
        if (i != CLEAR_BUTTON) {
            c.fillCircle(x + 10, cy, SWATCH_R, swatchOf(i));
            textX = x + 19;
        }
        c.setTextColor(on ? theme::LABEL : theme::LABEL_DIM);
        c.drawString(LABELS[i], textX, cy + 1);
    }
}

int toolbarButtonAt(int x, int screenWidth) {
    int i = x / (screenWidth / TOOL_BUTTONS);
    return i < 0 ? 0 : i >= TOOL_BUTTONS ? TOOL_BUTTONS - 1 : i;
}

const char* toolName(Tool tool) {
    return LABELS[static_cast<int>(tool)];
}
