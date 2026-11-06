#pragma once

#include <stdint.h>

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {

constexpr uint16_t BLACK = 0;
constexpr uint16_t EYE = rgb565(0x45, 0xc6, 0xf5);      // the buddy's cyan
constexpr uint16_t ANGRY = rgb565(0xff, 0x4d, 0x4d);
constexpr uint16_t LOVE = rgb565(0xff, 0x7e, 0xb6);
constexpr uint16_t SLEEP_Z = rgb565(0x2f, 0x86, 0xa8);  // dimmer cyan for the floating Zs

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

constexpr uint8_t BRIGHTNESS_AWAKE = 110;
constexpr uint8_t BRIGHTNESS_ASLEEP = 20;

}  // namespace theme
