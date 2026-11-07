#pragma once

namespace sand {

// Notices the board being shaken, from accelerometer readings at about 100 Hz. It watches how far the
// reading's strength strays from 1 g: turning the board over (to pour the sand the other way) only
// changes the reading's direction, while shaking it changes its strength. The smoothed amount has to
// climb past one level to start a shake and fall below a lower one to end it.
class ShakeDetector {
public:
    void update(float ax, float ay, float az);  // one reading, in g
    bool shaking() const { return _shaking; }
    float energy() const { return _energy; }    // smoothed distance from 1 g

private:
    float _energy = 0.0f;
    bool _shaking = false;
};

}  // namespace sand
