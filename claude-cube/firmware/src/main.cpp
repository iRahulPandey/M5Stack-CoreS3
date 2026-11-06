// Claude cube: shows the current Claude Code session on an M5Stack CoreS3.
// Frames arrive over USB serial from host/bridge.py. Shake the cube or tap the screen to flip pages.
#include <M5Unified.h>
#include <string.h>

#include "frameLink.h"
#include "pages.h"
#include "shakeDetector.h"

namespace {

constexpr uint32_t REDRAW_MS = 500;  // keeps the timers ticking without redrawing every loop
constexpr uint8_t SPEAKER_VOLUME = 110;
constexpr float CHIME_LOW_HZ = 1760.0f;
constexpr float CHIME_HIGH_HZ = 2349.0f;
constexpr uint32_t CHIME_GAP_MS = 130;
constexpr size_t SNAP_CHUNK_BYTES = 4096;
constexpr uint32_t SNAP_STALL_MS = 1000;  // give up if the host stops reading

M5Canvas canvas(&M5.Display);
FrameLink frameLink;
ShakeDetector shake;
SessionState session;

Page page = Page::Tokens;
bool dirty = true;
uint32_t lastDrawMs = 0;
uint32_t chimeSecondNoteAtMs = 0;
char lastState[sizeof(SessionState::state)] = "";

void startChime(uint32_t nowMs) {
    M5.Speaker.tone(CHIME_LOW_HZ, 90);
    chimeSecondNoteAtMs = nowMs + CHIME_GAP_MS;
}

void serviceChime(uint32_t nowMs) {
    if (chimeSecondNoteAtMs != 0 && nowMs >= chimeSecondNoteAtMs) {
        M5.Speaker.tone(CHIME_HIGH_HZ, 140);
        chimeSecondNoteAtMs = 0;
    }
}

// Claude just started waiting on you: chime once and jump to the Now page.
void onFrame(uint32_t nowMs) {
    bool waiting = strcmp(session.state, "waiting") == 0;
    if (waiting && strcmp(lastState, "waiting") != 0) {
        startChime(nowMs);
        page = Page::Now;
    }
    strlcpy(lastState, session.state, sizeof(lastState));
}

bool wasTapped() {
    return M5.Touch.getCount() > 0 && M5.Touch.getDetail().wasClicked();
}

// Sends the canvas after a "SNAP <w> <h> <page>" line as raw big-endian RGB565; host/snap.py makes a PNG.
void sendSnapshot() {
    Serial.printf("SNAP %d %d %d\n", canvas.width(), canvas.height(), static_cast<int>(page));
    const uint8_t* pixels = static_cast<const uint8_t*>(canvas.getBuffer());
    const size_t total = static_cast<size_t>(canvas.width()) * canvas.height() * 2;
    size_t sent = 0;
    uint32_t lastProgressMs = millis();
    while (sent < total && millis() - lastProgressMs < SNAP_STALL_MS) {
        size_t n = Serial.write(pixels + sent, min(SNAP_CHUNK_BYTES, total - sent));
        if (n > 0) {
            sent += n;
            lastProgressMs = millis();
        }
    }
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 0;  // FrameLink owns Serial
    cfg.internal_mic = false;  // mic and speaker share I2S; only the speaker is used
    M5.begin(cfg);
    frameLink.begin();
    M5.Speaker.setVolume(SPEAKER_VOLUME);

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(M5.Display.width(), M5.Display.height());
}

void loop() {
    uint32_t nowMs = millis();
    M5.update();

    if (frameLink.poll(session, nowMs)) {
        onFrame(nowMs);
        dirty = true;
    }
    if (shake.update(nowMs) || wasTapped() || frameLink.takeNextPageRequest()) {
        page = nextPage(page);
        dirty = true;
    }
    serviceChime(nowMs);

    bool snap = frameLink.takeSnapRequest();
    if (dirty || snap || nowMs - lastDrawMs >= REDRAW_MS) {
        drawPage(canvas, page, session, frameLink.isUp(session, nowMs), nowMs);
        canvas.pushSprite(0, 0);
        dirty = false;
        lastDrawMs = nowMs;
    }
    if (snap) {
        sendSnapshot();
    }
    delay(5);
}
