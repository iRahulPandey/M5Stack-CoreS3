#include "frameLink.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

namespace {

constexpr int FRAME_VERSION = 1;
constexpr uint32_t LINK_TIMEOUT_MS = 6000;  // the bridge sends at least every 2 s
constexpr size_t RX_BUFFER_BYTES = 2048;
constexpr const char* SNAP_COMMAND = "snap";
constexpr const char* NEXT_PAGE_COMMAND = "next";

void copyText(char* dst, size_t size, JsonVariantConst value) {
    strlcpy(dst, value | "", size);
}

}  // namespace

void FrameLink::begin() {
    Serial.setRxBufferSize(RX_BUFFER_BYTES);
    Serial.begin(115200);
}

bool FrameLink::poll(SessionState& state, uint32_t nowMs) {
    bool updated = false;
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') {
            continue;
        }
        if (c != '\n') {
            if (_len < FRAME_LINE_MAX - 1) {
                _line[_len++] = c;
            } else {
                _overflow = true;  // drop the rest of this line
            }
            continue;
        }
        _line[_len] = '\0';
        if (!_overflow && strcmp(_line, SNAP_COMMAND) == 0) {
            _snapRequested = true;
        } else if (!_overflow && strcmp(_line, NEXT_PAGE_COMMAND) == 0) {
            _nextPageRequested = true;
        } else if (!_overflow && _len > 0) {
            updated = apply(state, nowMs) || updated;
        }
        _len = 0;
        _overflow = false;
    }
    return updated;
}

bool FrameLink::takeSnapRequest() {
    bool requested = _snapRequested;
    _snapRequested = false;
    return requested;
}

bool FrameLink::takeNextPageRequest() {
    bool requested = _nextPageRequested;
    _nextPageRequested = false;
    return requested;
}

bool FrameLink::isUp(const SessionState& state, uint32_t nowMs) const {
    return state.valid && nowMs - state.receivedAtMs < LINK_TIMEOUT_MS;
}

bool FrameLink::apply(SessionState& state, uint32_t nowMs) const {
    JsonDocument doc;
    if (deserializeJson(doc, _line, _len) != DeserializationError::Ok || (doc["v"] | 0) != FRAME_VERSION) {
        return false;
    }
    SessionState next = state;
    next.valid = true;
    next.receivedAtMs = nowMs;
    next.seq = doc["seq"] | 0u;
    copyText(next.state, sizeof(next.state), doc["st"]);
    copyText(next.tool, sizeof(next.tool), doc["tool"]);
    copyText(next.detail, sizeof(next.detail), doc["det"]);
    copyText(next.project, sizeof(next.project), doc["proj"]);
    copyText(next.title, sizeof(next.title), doc["title"]);
    copyText(next.model, sizeof(next.model), doc["model"]);
    copyText(next.modelId, sizeof(next.modelId), doc["mid"]);
    copyText(next.effort, sizeof(next.effort), doc["eff"]);
    next.fast = (doc["fast"] | 0) != 0;
    next.tokensIn = doc["tin"] | 0ull;
    next.tokensOut = doc["tout"] | 0ull;
    next.tokensCacheRead = doc["tcr"] | 0ull;
    next.tokensCacheWrite = doc["tcw"] | 0ull;
    next.contextUsed = doc["ctx"] | 0ull;
    next.contextMax = doc["ctxmax"] | 0ull;
    next.costUsd = doc["cost"] | -1.0f;
    next.elapsedS = doc["el"] | 0u;
    next.stateForS = doc["sfor"] | 0u;
    next.turns = doc["turns"] | 0u;
    next.toolCalls = doc["tools"] | 0u;
    next.linesAdded = doc["la"] | 0u;
    next.linesRemoved = doc["lr"] | 0u;
    state = next;
    return true;
}
