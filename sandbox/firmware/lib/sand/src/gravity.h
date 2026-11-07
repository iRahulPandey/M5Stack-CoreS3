#pragma once

#include "world.h"

namespace sand {

struct Gravity {
    Direction down{0, 0};
    float strength = 0.0f;  // the share of g along the screen: 1 standing up, 0 lying flat
};

// Which way is down on the screen, from an accelerometer reading in the CoreS3's IMU frame: x to the
// screen's right, y towards its top edge, z out of the screen, pointing up (away from the ground) at
// rest. Only the reading's direction counts, so a jolt does not change it.
Gravity gravityFrom(float ax, float ay, float az);

// How many steps to run this frame: STEPS_PER_S_AT_1G with the board upright, fewer as it lies back,
// none at all once it is nearly flat, so a board lying on the desk keeps its sculpture.
class Pacer {
public:
    int steps(float strength, float dtS);

private:
    float _owed = 0.0f;
};

}  // namespace sand
