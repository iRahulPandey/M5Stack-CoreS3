#pragma once

#include <stdint.h>

// Latest frame from the Mac bridge. Field names follow the frame keys in host/claudecube/frame.py.
struct SessionState {
    bool valid = false;
    uint32_t seq = 0;
    uint32_t receivedAtMs = 0;

    char state[12] = "idle";
    char tool[41] = "";
    char detail[41] = "";
    char project[41] = "";
    char title[49] = "";
    char model[41] = "";
    char modelId[41] = "";
    char effort[13] = "";
    bool fast = false;

    uint64_t tokensIn = 0;
    uint64_t tokensOut = 0;
    uint64_t tokensCacheRead = 0;
    uint64_t tokensCacheWrite = 0;
    uint64_t contextUsed = 0;
    uint64_t contextMax = 0;

    float costUsd = -1.0f;
    uint32_t elapsedS = 0;
    uint32_t stateForS = 0;
    uint32_t turns = 0;
    uint32_t toolCalls = 0;
    uint32_t linesAdded = 0;
    uint32_t linesRemoved = 0;
};
