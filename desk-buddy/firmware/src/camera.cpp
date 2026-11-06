#include "camera.h"

#include <M5Unified.h>
#include <driver/i2c.h>
#include <esp_camera.h>
#include <esp_log.h>

#include <list>
#include <utility>

#include "human_face_detect_mnp01.hpp"
#include "human_face_detect_msr01.hpp"

namespace {

// M5Unified's internal bus on the S3. Under Arduino, M5GFX runs it through Wire1, which installs the
// ESP-IDF I2C driver, so the camera can share that driver instead of starting a second one on the
// same pins (which fails, and deleting it afterwards silences the IMU).
constexpr i2c_port_t BUS = I2C_NUM_1;
constexpr int FRAME_W = 320;
constexpr int CELL = 8;
constexpr uint8_t MOTION_THRESHOLD = 6;     // in 6-bit green units (0..63)
constexpr uint32_t FACE_EVERY_MS = 300;
constexpr uint32_t FACE_HOLD_MS = 1200;     // still "visible" this long after the last detection
constexpr uint32_t ABSENT_FOR_ARRIVAL_MS = 8000;
constexpr uint32_t STACK_BYTES = 16384;     // face detection needs a deep stack
constexpr UBaseType_t PRIORITY = 1;         // below the 100 Hz motion task on the same core
constexpr BaseType_t CORE = 0;

// Pins from M5Stack's own CoreS3 camera driver (M5CoreS3 src/utility/GC0308.cpp). XCLK is on the module.
camera_config_t cameraConfig() {
    camera_config_t c = {};
    c.pin_pwdn = -1;
    c.pin_reset = -1;
    c.pin_xclk = -1;
    c.pin_sccb_sda = -1;  // -1: use the I2C bus that is already running, by port number
    c.pin_sccb_scl = -1;
    c.pin_d7 = 47;
    c.pin_d6 = 48;
    c.pin_d5 = 16;
    c.pin_d4 = 15;
    c.pin_d3 = 42;
    c.pin_d2 = 41;
    c.pin_d1 = 40;
    c.pin_d0 = 39;
    c.pin_vsync = 46;
    c.pin_href = 38;
    c.pin_pclk = 45;
    c.xclk_freq_hz = 20000000;
    c.ledc_timer = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_RGB565;
    c.frame_size = FRAMESIZE_QVGA;
    c.jpeg_quality = 0;
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
    c.sccb_i2c_port = BUS;
    return c;
}

// The green channel (6 bits) of four pixels per 8 x 8 cell: enough to see a hand move.
void toGrid(const uint8_t* rgb565, uint8_t* grid) {
    for (int gy = 0; gy < CameraTask::GRID_H; gy++) {
        for (int gx = 0; gx < CameraTask::GRID_W; gx++) {
            int sum = 0;
            for (int dy : {2, 6}) {
                for (int dx : {2, 6}) {
                    const uint8_t* p = rgb565 + ((gy * CELL + dy) * FRAME_W + gx * CELL + dx) * 2;
                    sum += ((p[0] & 0x07) << 3) | (p[1] >> 5);
                }
            }
            grid[gy * CameraTask::GRID_W + gx] = static_cast<uint8_t>(sum / 4);
        }
    }
}

}  // namespace

bool CameraTask::begin(SemaphoreHandle_t i2cLock) {
    // The driver's own cam_task warns whenever it drops a frame we are too busy to take, and printing
    // that warning overflows its 2 KB stack (Arduino's CONFIG_CAMERA_TASK_STACK_SIZE). Dropped frames are
    // fine here; the logging is what crashes, so turn it off before the task starts.
    esp_log_level_set("cam_hal", ESP_LOG_NONE);
    esp_log_level_set("camera", ESP_LOG_NONE);
    camera_config_t config = cameraConfig();
    xSemaphoreTake(i2cLock, portMAX_DELAY);  // the sensor is configured over the shared bus
    esp_err_t err = esp_camera_init(&config);
    xSemaphoreGive(i2cLock);
    bool ok = err == ESP_OK;
    _local.ok = _shared.ok = ok;
    _local.error = _shared.error = err;
    if (ok) xTaskCreatePinnedToCore(run, "camera", STACK_BYTES, this, PRIORITY, nullptr, CORE);
    return ok;
}

CameraState CameraTask::snapshot() {
    portENTER_CRITICAL(&_mux);
    CameraState copy = _shared;
    portEXIT_CRITICAL(&_mux);
    return copy;
}

void CameraTask::run(void* self) {
    static_cast<CameraTask*>(self)->loop();
}

void CameraTask::loop() {
    HumanFaceDetectMSR01 stage1(0.1F, 0.5F, 10, 0.2F);
    HumanFaceDetectMNP01 stage2(0.5F, 0.3F, 5);
    uint8_t* previous = _gridA;
    uint8_t* current = _gridB;
    bool havePrevious = false;
    uint32_t lastFaceRunMs = 0;
    uint32_t lastFrameMs = millis();

    for (;;) {
        camera_fb_t* fb = esp_camera_fb_get();
        if (fb == nullptr) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        uint32_t nowMs = millis();
        toGrid(fb->buf, current);
        uint32_t sum = 0;
        for (uint8_t v : _gridA == current ? _gridA : _gridB) sum += v;
        _local.brightness = static_cast<uint8_t>(sum / (GRID_W * GRID_H));
        buddy::FrameMotion motion;
        if (havePrevious) motion = buddy::diffFrames(previous, current, GRID_W, GRID_H, MOTION_THRESHOLD);
        bool waved = havePrevious && _waves.update(motion, nowMs);
        std::swap(previous, current);
        havePrevious = true;

        if (nowMs - lastFaceRunMs >= FACE_EVERY_MS) {
            lastFaceRunMs = nowMs;
            uint32_t t0 = millis();
            std::list<dl::detect::result_t>& candidates =
                stage1.infer(reinterpret_cast<uint16_t*>(fb->buf), {static_cast<int>(fb->height), static_cast<int>(fb->width), 3});
            std::list<dl::detect::result_t>& faces = stage2.infer(
                reinterpret_cast<uint16_t*>(fb->buf), {static_cast<int>(fb->height), static_cast<int>(fb->width), 3}, candidates);
            _local.faceMs = millis() - t0;
            const dl::detect::result_t* best = nullptr;
            int bestArea = 0;
            for (const auto& f : faces) {  // the biggest face is the closest person
                int area = (f.box[2] - f.box[0]) * (f.box[3] - f.box[1]);
                if (area > bestArea) {
                    bestArea = area;
                    best = &f;
                }
            }
            float fx = best ? ((best->box[0] + best->box[2]) / 2.0f) / fb->width * 2.0f - 1.0f : 0.0f;
            float fy = best ? ((best->box[1] + best->box[3]) / 2.0f) / fb->height * 2.0f - 1.0f : 0.0f;
            trackFace(best != nullptr, fx, fy, nowMs);
        }
        esp_camera_fb_return(fb);
        publish(motion, waved, nowMs - lastFrameMs, nowMs);
        lastFrameMs = nowMs;
        vTaskDelay(1);
    }
}

void CameraTask::trackFace(bool found, float x, float y, uint32_t nowMs) {
    if (!found) return;
    _local.faceFrames++;
    _local.faceX = x;
    _local.faceY = y;
    bool arrived = !_local.faceVisible && (!_everSawFace || nowMs - _faceGoneSinceMs >= ABSENT_FOR_ARRIVAL_MS);
    if (arrived) _local.arrivals++;
    _lastFaceMs = nowMs;
    _everSawFace = true;
    _local.faceVisible = true;
}

// Runs every frame: visibility expires between detections, and the shared copy is refreshed.
void CameraTask::publish(const buddy::FrameMotion& motion, bool waved, uint32_t frameMs, uint32_t nowMs) {
    bool wasVisible = _local.faceVisible;
    _local.faceVisible = _everSawFace && nowMs - _lastFaceMs < FACE_HOLD_MS;
    if (wasVisible && !_local.faceVisible) _faceGoneSinceMs = nowMs;
    if (waved) _local.waves++;
    _local.motion = motion.amount;
    if (frameMs > 0) _local.fps += 0.1f * (1000.0f / frameMs - _local.fps);
    portENTER_CRITICAL(&_mux);
    _shared = _local;
    portEXIT_CRITICAL(&_mux);
}
