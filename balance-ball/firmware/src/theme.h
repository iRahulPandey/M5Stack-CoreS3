#pragma once

#include <stdint.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {

// The table it rolls on
constexpr uint16_t FLOOR = rgb565(0x14, 0x18, 0x2e);
constexpr uint16_t GRID = rgb565(0x1d, 0x23, 0x40);
constexpr uint16_t RING = rgb565(0x34, 0x3d, 0x6b);
constexpr uint16_t HOME = rgb565(0x3d, 0xd6, 0x8c);  // the ring once it is in the middle
constexpr uint16_t SHADOW = rgb565(0x0b, 0x0d, 0x1a);
constexpr uint16_t WALL_HIT = rgb565(0xff, 0x3b, 0x30);
constexpr uint16_t TEXT_DIM = rgb565(0x8a, 0x93, 0xb8);
constexpr uint16_t TEXT_BEST = rgb565(0xff, 0xd2, 0x4a);

// The ball, in its usual sunny yellow and in angry red; redness blends between them
constexpr uint16_t BODY = rgb565(0xff, 0xd2, 0x4a);
constexpr uint16_t BODY_SHADE = rgb565(0xe0, 0x9a, 0x1c);
constexpr uint16_t BODY_SHINE = rgb565(0xff, 0xf3, 0xc4);
constexpr uint16_t RED = rgb565(0xe8, 0x33, 0x2a);
constexpr uint16_t RED_SHADE = rgb565(0x9e, 0x16, 0x16);
constexpr uint16_t RED_SHINE = rgb565(0xff, 0x9a, 0x8a);

constexpr uint16_t INK = rgb565(0x2a, 0x17, 0x0c);  // features
constexpr uint16_t EYE_WHITE = 0xFFFF;
constexpr uint16_t BLUSH = rgb565(0xff, 0x8f, 0x8f);
constexpr uint16_t SWEAT = rgb565(0x7c, 0xc8, 0xff);
constexpr uint16_t STEAM = rgb565(0xf0, 0xf0, 0xf4);
constexpr uint16_t VEIN = rgb565(0xff, 0x3b, 0x30);

constexpr uint8_t BRIGHTNESS = 120;

// Mixes two colours: t = 0 gives a, t = 1 gives b.
inline uint16_t blend(uint16_t a, uint16_t b, float t) {
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    auto mix = [t](int x, int y) { return static_cast<int>(x + (y - x) * t + 0.5f); };
    int r = mix(a >> 11, b >> 11);
    int g = mix((a >> 5) & 0x3F, (b >> 5) & 0x3F);
    int bl = mix(a & 0x1F, b & 0x1F);
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

}  // namespace theme
