#pragma once

#include <stdint.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {

struct Rgb {
    uint8_t r, g, b;
};

struct Colour {
    const char* name;
    Rgb rgb;
};

// The coats, in order: the dullest shades a paint chart has to offer.
constexpr Colour COATS[] = {
    {"Magnolia", {0xee, 0xe2, 0xc9}},
    {"Greige", {0xc9, 0xc0, 0xb0}},
    {"Eggshell", {0xf0, 0xea, 0xd6}},
    {"Beige", {0xdd, 0xc9, 0xa3}},
    {"Off-White", {0xf4, 0xf1, 0xe8}},
    {"Light Grey", {0xd3, 0xd3, 0xcf}},
};
constexpr int COAT_COUNT = sizeof(COATS) / sizeof(COATS[0]);

inline const Colour& coatColour(int coat) {
    return COATS[(coat - 1) % COAT_COUNT];
}

constexpr float WET_SHADE = 0.8f;  // wet paint looks this much darker

constexpr uint16_t CARD = rgb565(0xfb, 0xfa, 0xf6);
constexpr uint16_t CARD_EDGE = rgb565(0xb8, 0xb2, 0xa6);
constexpr uint16_t INK = rgb565(0x3a, 0x38, 0x33);
constexpr uint16_t INK_DIM = rgb565(0x8a, 0x86, 0x7d);
constexpr uint16_t BAR = rgb565(0xe2, 0xde, 0xd4);
constexpr uint16_t BAR_FILL = rgb565(0x7d, 0x93, 0x7a);
constexpr uint16_t SIGN = rgb565(0xff, 0xff, 0xff);
constexpr uint16_t SIGN_RED = rgb565(0xd6, 0x2b, 0x2b);
constexpr uint16_t POST = rgb565(0x8b, 0x6b, 0x47);

constexpr uint8_t BRIGHTNESS = 110;

}  // namespace theme
