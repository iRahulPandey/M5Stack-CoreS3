#pragma once

#include <M5Unified.h>

#include "mood.h"
#include "motion.h"

// The buddy's face: two eyes that ease between poses, blink, glance around,
// and switch to arcs, spirals, hearts or closed lids for some moods.
class Eyes {
public:
    void setMood(buddy::Mood mood, uint32_t nowMs);
    void update(const buddy::MotionState& motion, uint32_t nowMs);
    void draw(M5Canvas& canvas, uint32_t nowMs) const;

private:
    struct Pose {
        float w = 70.0f, h = 100.0f, r = 22.0f;  // each eye
        float lookX = 0.0f, lookY = 0.0f;        // both eyes shift this far, in pixels
        float lid = 0.0f;                        // share of the eye hidden from the top
        float slant = 0.0f;                      // angry brow: inner top corners cut down
        float leftScale = 1.0f, rightScale = 1.0f;
    };

    Pose targetFor(buddy::Mood mood) const;
    void updateGlance(uint32_t nowMs);
    void updateBlink(uint32_t nowMs);
    float blinkFactor(uint32_t nowMs) const;
    void drawOpenEyes(M5Canvas& c, uint32_t nowMs) const;
    void drawExcited(M5Canvas& c, uint32_t nowMs) const;

    buddy::Mood _mood = buddy::Mood::Idle;
    uint32_t _moodSinceMs = 0;
    Pose _pose;
    float _glanceX = 0.0f, _glanceY = 0.0f;
    uint32_t _nextGlanceMs = 0;
    uint32_t _nextBlinkMs = 0;
    uint32_t _blinkStartMs = 0;
    bool _blinking = false;
    float _energy = 0.0f;
    float _leanX = 0.0f, _leanY = 0.0f;
};
