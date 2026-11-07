#pragma once

#include <stdint.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {

struct Rgb {
    uint8_t r, g, b;
};

// Each grain gets a shade between these two, so a heap looks grainy
constexpr Rgb SAND_DARK{0xc9, 0x92, 0x4a};
constexpr Rgb SAND_LIGHT{0xf7, 0xd9, 0x92};
constexpr Rgb WATER_DARK{0x1f, 0x5f, 0xd0};
constexpr Rgb WATER_LIGHT{0x6a, 0xb4, 0xff};
constexpr Rgb WALL_DARK{0x5c, 0x60, 0x72};
constexpr Rgb WALL_LIGHT{0x9a, 0x9e, 0xb0};
// The empty box, darker at the top
constexpr Rgb SKY_TOP{0x10, 0x13, 0x26};
constexpr Rgb SKY_BOTTOM{0x1d, 0x23, 0x40};

constexpr uint16_t BAR = rgb565(0x0b, 0x0d, 0x1a);
constexpr uint16_t BUTTON = rgb565(0x23, 0x29, 0x45);
constexpr uint16_t BUTTON_ON = rgb565(0x3a, 0x43, 0x6e);
constexpr uint16_t LABEL = rgb565(0xc8, 0xcd, 0xe6);
constexpr uint16_t LABEL_DIM = rgb565(0x7a, 0x82, 0xa6);
constexpr uint16_t ERASE = rgb565(0xff, 0x6b, 0x6b);

constexpr uint8_t BRIGHTNESS = 120;

}  // namespace theme
