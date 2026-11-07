#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <vector>

#include "gravity.h"
#include "motionTask.h"
#include "world.h"

// A change to the world asked for by touch or serial; the sim task applies it before its next step.
struct Edit {
    enum class Kind : uint8_t { Paint, Line, Clear, Scene };
    Kind kind = Kind::Paint;
    sand::Material material = sand::Material::Sand;
    int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint8_t radius = 0;
    uint8_t density = 100;
};

struct SimStats {
    sand::Gravity gravity;
    bool shaking = false;
    bool pretending = false;  // gravity comes from the Mac, not the board
    unsigned long steps = 0;
    float stepMs = 0.0f;      // average time one step takes
};

// Runs the sand on core 0, beside the IMU task. It alone touches the world: edits arrive through a
// queue, and after every step it copies the grid into a frame that core 1 draws from. So neither core
// waits on the other for longer than that 17 KB copy, and more grains slow the sand, not the screen.
class SimTask {
public:
    void begin(sand::World& world, MotionTask& motion, void (*scene)(sand::World&));
    void edit(const Edit& e);  // dropped if 32 edits are already waiting
    const uint8_t* lockFrame();  // the latest grid, row by row; unlockFrame() when done reading it
    void unlockFrame();
    int frameWidth() const { return _world->width(); }
    int frameHeight() const { return _world->height(); }
    void pretendGravity(sand::Direction down, uint32_t untilMs);  // for screenshots and tests without hands
    void pretendShake(uint32_t untilMs);
    SimStats stats();

private:
    static void run(void* self);
    void loop();
    void tick(uint32_t nowMs, float dtS);
    void apply(const Edit& e);
    void publish();

    sand::World* _world = nullptr;
    MotionTask* _motion = nullptr;
    void (*_scene)(sand::World&) = nullptr;
    QueueHandle_t _edits = nullptr;
    SemaphoreHandle_t _frameLock = nullptr;
    std::vector<uint8_t> _frame;
    sand::Pacer _pacer;
    uint32_t _lastStirMs = 0;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;  // guards the fields below
    sand::Direction _pretendDown{0, 1};
    uint32_t _pretendUntilMs = 0;
    uint32_t _shakeUntilMs = 0;
    SimStats _stats;
};
