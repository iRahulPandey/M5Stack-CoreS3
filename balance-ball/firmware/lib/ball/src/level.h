#pragma once

namespace ball {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Turns accelerometer readings into how far the board leans away from the pose the player calls
// level, as the downhill pull a ball on the screen would feel.
// Readings are in the CoreS3's IMU frame: x to the right of the screen, y towards its top edge, z out
// of the screen. At rest they point up, away from the ground: lying screen-up reads (0, 0, 1).
class Level {
public:
    Level();  // level starts as lying flat, screen up

    // The reading while the board is held the way the player wants to call level.
    void set(float ax, float ay, float az);

    // Downhill pull in g along the screen: x to the right, y down the screen. 0 when level, 1 when
    // leaning a quarter turn. Only the direction of the reading matters, not its strength.
    Vec2 pull(float ax, float ay, float az) const;

private:
    float _right[3];  // the screen's right and top directions, laid flat in the level pose
    float _top[3];
};

}  // namespace ball
