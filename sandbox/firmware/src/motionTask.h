#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "shake.h"

struct Motion {
    float x = 0.0f, y = 1.0f, z = 0.0f;  // accelerometer reading in g, smoothed against hand jitter
    bool shaking = false;
    float shakeEnergy = 0.0f;
};

// Reads the IMU 100 times a second on core 0, while core 1 spends ~30 ms of each frame pushing it
// to the screen; a shake needs every sample. Shares the internal I2C bus with touch through `i2cLock`.
class MotionTask {
public:
    void begin(SemaphoreHandle_t i2cLock);
    Motion snapshot();

private:
    static void run(void* self);
    void loop();

    SemaphoreHandle_t _i2cLock = nullptr;
    bool _started = false;
    Motion _smoothed;
    Motion _shared;
    sand::ShakeDetector _shake;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};
