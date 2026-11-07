#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// An accelerometer reading in g, smoothed just enough to take out the hand's jitter.
struct Reading {
    float x = 0.0f, y = 0.0f, z = 1.0f;
};

// Reads the IMU 100 times a second on core 0, while core 1 spends ~30 ms of each frame pushing it
// to the screen. Shares the internal I2C bus with touch through `i2cLock`.
class MotionTask {
public:
    void begin(SemaphoreHandle_t i2cLock);
    Reading snapshot();

private:
    static void run(void* self);
    void loop();

    SemaphoreHandle_t _i2cLock = nullptr;
    bool _started = false;
    Reading _smoothed;
    Reading _shared;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};
