#pragma once

#include <stdint.h>

// Midnight Crema, the iPandeyRahul channel palette, as RGB565 for the 16-bit canvas.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {

constexpr uint16_t ROAST = rgb565(0x0a, 0x13, 0x22);     // background
constexpr uint16_t SAUCER = rgb565(0x0e, 0x19, 0x30);    // raised surfaces
constexpr uint16_t GROUNDS = rgb565(0x1c, 0x2c, 0x4a);   // lines, inactive
constexpr uint16_t FOAM = rgb565(0xea, 0xf1, 0xfc);      // primary text
constexpr uint16_t STEAM = rgb565(0x82, 0x99, 0xbd);     // labels
constexpr uint16_t CREMA = rgb565(0xf6, 0xa4, 0x1c);     // accent, big numbers
constexpr uint16_t CHERRY = rgb565(0xff, 0x5d, 0x5d);    // needs you, near limit
constexpr uint16_t COLDBREW = rgb565(0x58, 0xa6, 0xff);  // working

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int MARGIN = 12;

}  // namespace theme
