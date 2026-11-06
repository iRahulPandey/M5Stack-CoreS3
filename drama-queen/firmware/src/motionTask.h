#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "motion.h"

// Reads the IMU 100 times a second on core 0, so a short slap is not missed while core 1
// spends ~30 ms pushing a frame to the screen. Shares the internal I2C bus through `i2cLock`.
class MotionTask {
public:
    void begin(SemaphoreHandle_t i2cLock);
    drama::MotionState snapshot();

private:
    static void run(void* self);
    void loop();

    SemaphoreHandle_t _i2cLock = nullptr;
    drama::MotionClassifier _classifier;
    drama::MotionState _shared;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
};
