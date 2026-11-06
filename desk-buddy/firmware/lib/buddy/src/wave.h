#pragma once

#include <stdint.h>

namespace buddy {

// Where things moved between two small grayscale frames.
struct FrameMotion {
    float amount = 0.0f;   // share of cells that changed, 0..1
    float centerX = 0.5f;  // centre of the changed cells, 0 (left of the image) .. 1 (right)
    float centerY = 0.5f;
};

// Compares two w*h grayscale frames cell by cell; a cell "moved" when it changed by more than `threshold`.
FrameMotion diffFrames(const uint8_t* previous, const uint8_t* current, int w, int h, uint8_t threshold);

// A wave is a patch of motion swinging left and right a few times in quick succession.
// Walking across the frame (one sweep) or the whole picture changing (lights, a bump) is not.
class WaveDetector {
public:
    // Returns true once per wave.
    bool update(const FrameMotion& motion, uint32_t nowMs);

private:
    static constexpr int MAX_TURNS = 8;

    void reset();

    bool _tracking = false;
    float _anchorX = 0.0f;     // where the current swing started
    float _lastX = 0.0f;
    int _direction = 0;        // +1 moving right, -1 moving left, 0 unknown
    uint32_t _turnsMs[MAX_TURNS] = {};
    int _turns = 0;
    uint32_t _lastSeenMs = 0;
    uint32_t _cooldownUntilMs = 0;
};

}  // namespace buddy
