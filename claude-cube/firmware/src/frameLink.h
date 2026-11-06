#pragma once

#include <stddef.h>

#include "sessionState.h"

// Reads newline-delimited JSON frames from the USB serial port.
class FrameLink {
public:
    void begin();
    // Returns true when a new valid frame was copied into `state`.
    bool poll(SessionState& state, uint32_t nowMs);
    bool isUp(const SessionState& state, uint32_t nowMs) const;
    // Each is true once after the host sent "snap" (send a screenshot) or "next" (flip the page).
    bool takeSnapRequest();
    bool takeNextPageRequest();

private:
    static constexpr size_t FRAME_LINE_MAX = 1024;  // LINE_MAX is already a libc macro
    char _line[FRAME_LINE_MAX];
    size_t _len = 0;
    bool _overflow = false;
    bool _snapRequested = false;
    bool _nextPageRequested = false;

    bool apply(SessionState& state, uint32_t nowMs) const;
};
