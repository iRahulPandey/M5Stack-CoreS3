#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdint.h>

#include "wave.h"

// What the camera has seen, copied out of the camera task.
struct CameraState {
    bool ok = false;           // the camera started
    int error = 0;             // esp_camera_init() result when it did not
    bool faceVisible = false;  // a face was found in the last ~1.2 s
    float faceX = 0.0f;        // face centre in the image: -1 left .. 1 right
    float faceY = 0.0f;        // -1 top .. 1 bottom
    uint32_t waves = 0;        // event counters: a change means a new event
    uint32_t arrivals = 0;     // a face appeared after nobody was there for a while
    uint32_t faceFrames = 0;   // detections that found a face
    float fps = 0.0f;
    uint32_t faceMs = 0;       // how long the last face detection took
    float motion = 0.0f;       // share of the picture that changed between the last two frames
    uint8_t brightness = 0;    // average of the motion grid, 0..63: near 0 means the camera sees nothing
};

// Runs the GC0308 camera on core 0: frame-to-frame motion for waves, face detection a few times a second.
class CameraTask {
public:
    // Call after M5.begin(), before anything else uses the internal I2C bus. False if the camera did not start.
    bool begin(SemaphoreHandle_t i2cLock);
    CameraState snapshot();

    static constexpr int GRID_W = 40;  // motion grid: one cell per 8 x 8 pixels of the 320 x 240 frame
    static constexpr int GRID_H = 30;

private:
    static void run(void* self);
    void loop();
    void trackFace(bool found, float x, float y, uint32_t nowMs);
    void publish(const buddy::FrameMotion& motion, bool waved, uint32_t frameMs, uint32_t nowMs);

    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    CameraState _shared;
    CameraState _local;
    buddy::WaveDetector _waves;
    uint8_t _gridA[GRID_W * GRID_H] = {};
    uint8_t _gridB[GRID_W * GRID_H] = {};
    uint32_t _lastFaceMs = 0;
    uint32_t _faceGoneSinceMs = 0;
    bool _everSawFace = false;
};
