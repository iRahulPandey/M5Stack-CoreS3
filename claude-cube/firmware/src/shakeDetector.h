#pragma once

#include <stdint.h>

// One shake = at least two sharp jolts close together, so setting the cube down does not count.
class ShakeDetector {
public:
    bool update(uint32_t nowMs);

private:
    bool _inJolt = false;
    uint8_t _jolts = 0;
    uint32_t _firstJoltMs = 0;
    uint32_t _lastShakeMs = 0;
};
