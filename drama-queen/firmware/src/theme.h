#pragma once

#include <stdint.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {

constexpr uint16_t PAPER = 0xFFFF;
constexpr uint16_t INK = rgb565(0x22, 0x1a, 0x2e);           // outlines and pupils
constexpr uint16_t SKIN = rgb565(0xff, 0xc4, 0xd6);          // her usual pink
constexpr uint16_t SKIN_SICK = rgb565(0xb9, 0xe4, 0x9c);
constexpr uint16_t SKIN_CROSS = rgb565(0xff, 0x7a, 0x6e);
constexpr uint16_t SKIN_FURIOUS = rgb565(0xe5, 0x3b, 0x3b);
constexpr uint16_t SKIN_SAD = rgb565(0xa9, 0xc8, 0xff);
constexpr uint16_t SKIN_SCREAM = rgb565(0xff, 0xe0, 0x6b);
constexpr uint16_t SKIN_DIZZY = rgb565(0xd9, 0xc7, 0xff);
constexpr uint16_t SKIN_FAINT = rgb565(0xe8, 0xe4, 0xea);
constexpr uint16_t NIGHT = rgb565(0x2a, 0x2c, 0x55);
constexpr uint16_t MOUTH = rgb565(0x8e, 0x1b, 0x3a);
constexpr uint16_t TONGUE = rgb565(0xff, 0x6f, 0x91);
constexpr uint16_t BLUSH = rgb565(0xff, 0x8f, 0xb1);
constexpr uint16_t TEAR = rgb565(0x5a, 0xb8, 0xff);
constexpr uint16_t CROWN = rgb565(0xff, 0xc8, 0x3d);
constexpr uint16_t GEM = rgb565(0xe0, 0x3a, 0x8c);
constexpr uint16_t HEART = rgb565(0xff, 0x3d, 0x7f);
constexpr uint16_t STAR = rgb565(0xff, 0xe1, 0x4d);
constexpr uint16_t STEAM = rgb565(0xf5, 0xf5, 0xf5);
constexpr uint16_t SNORE = rgb565(0xc9, 0xcd, 0xff);

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr uint8_t BRIGHTNESS_AWAKE = 120;
constexpr uint8_t BRIGHTNESS_ASLEEP = 25;

}  // namespace theme
