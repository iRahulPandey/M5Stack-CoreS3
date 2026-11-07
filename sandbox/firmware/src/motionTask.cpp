#include "motionTask.h"

#include <M5Unified.h>

namespace {

constexpr uint32_t PERIOD_MS = 10;
constexpr uint32_t STACK_BYTES = 4096;
constexpr UBaseType_t PRIORITY = 3;
constexpr BaseType_t CORE = 0;
constexpr float SMOOTHING = 0.2f;  // ~50 ms: gravity turns with the board, not with every wobble

}  // namespace

void MotionTask::begin(SemaphoreHandle_t i2cLock) {
    _i2cLock = i2cLock;
    xTaskCreatePinnedToCore(run, "motion", STACK_BYTES, this, PRIORITY, nullptr, CORE);
}

Motion MotionTask::snapshot() {
    portENTER_CRITICAL(&_mux);
    Motion copy = _shared;
    portEXIT_CRITICAL(&_mux);
    return copy;
}

void MotionTask::run(void* self) {
    static_cast<MotionTask*>(self)->loop();
}

void MotionTask::loop() {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        float ax = 0, ay = 0, az = 0;
        bool read = false;
        if (xSemaphoreTake(_i2cLock, pdMS_TO_TICKS(PERIOD_MS)) == pdTRUE) {
            read = M5.Imu.update() && M5.Imu.getAccel(&ax, &ay, &az);
            xSemaphoreGive(_i2cLock);
        }
        if (read) {
            if (!_started) {
                _smoothed.x = ax;
                _smoothed.y = ay;
                _smoothed.z = az;
                _started = true;
            }
            _smoothed.x += SMOOTHING * (ax - _smoothed.x);
            _smoothed.y += SMOOTHING * (ay - _smoothed.y);
            _smoothed.z += SMOOTHING * (az - _smoothed.z);
            _shake.update(ax, ay, az);
            _smoothed.shaking = _shake.shaking();
            _smoothed.shakeEnergy = _shake.energy();
            portENTER_CRITICAL(&_mux);
            _shared = _smoothed;
            portEXIT_CRITICAL(&_mux);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(PERIOD_MS));
    }
}
