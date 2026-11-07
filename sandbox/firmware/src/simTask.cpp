#include "simTask.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

namespace {

constexpr uint32_t TICK_MS = 5;
constexpr float MAX_STEP_S = 0.1f;
constexpr uint32_t STIR_EVERY_MS = 50;  // while shaking
constexpr int STIR_PERCENT = 20;        // of the loose grains, each time
constexpr UBaseType_t QUEUED_EDITS = 32;
constexpr uint32_t STACK_BYTES = 4096;
constexpr UBaseType_t PRIORITY = 2;     // below the IMU task, which must not miss samples
constexpr BaseType_t CORE = 0;

}  // namespace

void SimTask::begin(sand::World& world, MotionTask& motion, void (*scene)(sand::World&)) {
    _world = &world;
    _motion = &motion;
    _scene = scene;
    _edits = xQueueCreate(QUEUED_EDITS, sizeof(Edit));
    _frameLock = xSemaphoreCreateMutex();
    _frame.assign(world.cells(), world.cells() + static_cast<size_t>(world.width()) * world.height());
    xTaskCreatePinnedToCore(run, "sand", STACK_BYTES, this, PRIORITY, nullptr, CORE);
}

void SimTask::edit(const Edit& e) {
    xQueueSend(_edits, &e, 0);
}

const uint8_t* SimTask::lockFrame() {
    xSemaphoreTake(_frameLock, portMAX_DELAY);
    return _frame.data();
}

void SimTask::unlockFrame() {
    xSemaphoreGive(_frameLock);
}

void SimTask::pretendGravity(sand::Direction down, uint32_t untilMs) {
    portENTER_CRITICAL(&_mux);
    _pretendDown = down;
    _pretendUntilMs = untilMs;
    portEXIT_CRITICAL(&_mux);
}

void SimTask::pretendShake(uint32_t untilMs) {
    portENTER_CRITICAL(&_mux);
    _shakeUntilMs = untilMs;
    portEXIT_CRITICAL(&_mux);
}

SimStats SimTask::stats() {
    portENTER_CRITICAL(&_mux);
    SimStats copy = _stats;
    portEXIT_CRITICAL(&_mux);
    return copy;
}

void SimTask::run(void* self) {
    static_cast<SimTask*>(self)->loop();
}

void SimTask::loop() {
    uint32_t lastMs = millis();
    for (;;) {
        uint32_t nowMs = millis();
        tick(nowMs, fminf((nowMs - lastMs) / 1000.0f, MAX_STEP_S));
        lastMs = nowMs;
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));  // also lets core 0's idle task feed the watchdog
    }
}

void SimTask::apply(const Edit& e) {
    switch (e.kind) {
        case Edit::Kind::Paint: _world->paint(e.x0, e.y0, e.radius, e.material, e.density); break;
        case Edit::Kind::Line: _world->paintLine(e.x0, e.y0, e.x1, e.y1, e.radius, e.material, e.density); break;
        case Edit::Kind::Clear: _world->clear(); break;
        case Edit::Kind::Scene: _scene(*_world); break;
    }
}

void SimTask::publish() {
    xSemaphoreTake(_frameLock, portMAX_DELAY);
    memcpy(_frame.data(), _world->cells(), _frame.size());
    xSemaphoreGive(_frameLock);
}

void SimTask::tick(uint32_t nowMs, float dtS) {
    Edit e;
    bool changed = false;
    while (xQueueReceive(_edits, &e, 0) == pdTRUE) {
        apply(e);
        changed = true;
    }
    Motion motion = _motion->snapshot();
    portENTER_CRITICAL(&_mux);
    bool pretending = nowMs < _pretendUntilMs;
    sand::Direction pretendDown = _pretendDown;
    bool shaking = motion.shaking || nowMs < _shakeUntilMs;
    portEXIT_CRITICAL(&_mux);

    sand::Gravity g = sand::gravityFrom(motion.x, motion.y, motion.z);
    if (pretending) {
        g.down = pretendDown;
        g.strength = pretendDown.dx == 0 && pretendDown.dy == 0 ? 0.0f : 1.0f;
    }
    int n = _pacer.steps(g.strength, dtS);
    uint32_t startUs = micros();
    for (int i = 0; i < n; i++) _world->step(g.down);
    float stepMs = n > 0 ? (micros() - startUs) / 1000.0f / n : 0.0f;
    bool stirring = shaking && nowMs - _lastStirMs >= STIR_EVERY_MS;
    if (stirring) {
        _world->stir(STIR_PERCENT);
        _lastStirMs = nowMs;
    }
    if (changed || n > 0 || stirring) publish();

    portENTER_CRITICAL(&_mux);
    _stats.gravity = g;
    _stats.shaking = shaking;
    _stats.pretending = pretending;
    _stats.steps += n;
    if (n > 0) _stats.stepMs += 0.1f * (stepMs - _stats.stepMs);
    portEXIT_CRITICAL(&_mux);
}
