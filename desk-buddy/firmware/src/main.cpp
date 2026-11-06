// Desk buddy: two expressive eyes that react to how you treat the cube.
// Shake gently: happy. Shake harder: excited. Shake hard for a while: dizzy. Slap it: angry.
// Bump it: surprised. Stroke the screen: loved. Tilt it or bring a hand close: curious.
// Leave it alone: bored, then asleep.
// Serial commands, one per line: status, mood <name> (show a mood now), snap (screenshot).
#include <M5Unified.h>
#include <math.h>
#include <string.h>

#include "eyes.h"
#include "mood.h"
#include "motionTask.h"
#include "proximity.h"
#include "theme.h"
#include "voice.h"

using buddy::Mood;

namespace {

constexpr uint32_t FRAME_MS = 33;
constexpr uint8_t SPEAKER_VOLUME = 90;
constexpr size_t COMMAND_MAX = 40;
constexpr size_t RX_BUFFER_BYTES = 1024;
constexpr size_t SNAP_CHUNK_BYTES = 4096;
constexpr uint32_t SNAP_STALL_MS = 1000;

M5Canvas canvas(&M5.Display);
SemaphoreHandle_t i2cLock;
MotionTask motion;
Proximity proximity;
buddy::MoodEngine moodEngine;
Eyes eyes;
Voice voice;

buddy::MotionState lastMotion;
uint32_t seenSlaps = 0;
uint32_t seenTaps = 0;
float framesPerSecond = 0.0f;

char command[COMMAND_MAX];
size_t commandLen = 0;
bool commandOverflow = false;

struct TouchInput {
    bool poke = false;
    bool stroking = false;
};

// Touch and proximity share the internal I2C bus with the IMU task.
TouchInput readSharedBus(uint32_t nowMs) {
    TouchInput touch;
    if (xSemaphoreTake(i2cLock, pdMS_TO_TICKS(5)) != pdTRUE) return touch;
    M5.update();
    if (M5.Touch.getCount() > 0) {
        auto d = M5.Touch.getDetail();
        touch.poke = d.wasClicked();
        touch.stroking = d.isDragging() || d.isFlicking();
    }
    proximity.update(nowMs);
    xSemaphoreGive(i2cLock);
    return touch;
}

void onMood(Mood mood, uint32_t nowMs) {
    eyes.setMood(mood, nowMs);
    voice.play(mood);
    M5.Display.setBrightness(mood == Mood::Asleep ? theme::BRIGHTNESS_ASLEEP : theme::BRIGHTNESS_AWAKE);
}

void printStatus() {
    const auto& m = lastMotion;
    Serial.printf(
        "STATUS {\"mood\":\"%s\",\"grump\":%u,\"shake\":%u,\"energy\":%.2f,\"leanX\":%.2f,\"leanY\":%.2f,"
        "\"tilted\":%s,\"slaps\":%lu,\"taps\":%lu,\"prox\":%u,\"proxBase\":%u,\"near\":%s,\"proxOk\":%s,"
        "\"fps\":%.1f}\n",
        buddy::moodName(moodEngine.mood()), moodEngine.grumpiness(), static_cast<unsigned>(m.shake), m.energy,
        m.leanX, m.leanY, m.tilted ? "true" : "false", static_cast<unsigned long>(m.slaps),
        static_cast<unsigned long>(m.taps), proximity.raw(), proximity.baseline(), proximity.near() ? "true" : "false",
        proximity.enabled() ? "true" : "false", framesPerSecond);
}

void sendSnapshot() {
    Serial.printf("SNAP %d %d %d\n", canvas.width(), canvas.height(), static_cast<int>(moodEngine.mood()));
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

void handleCommand(const char* cmd, uint32_t nowMs) {
    Mood forced;
    if (strcmp(cmd, "status") == 0) {
        printStatus();
    } else if (strcmp(cmd, "snap") == 0) {
        sendSnapshot();
    } else if (strncmp(cmd, "mood ", 5) == 0 && buddy::moodFromName(cmd + 5, forced)) {
        moodEngine.force(forced, nowMs);
        onMood(forced, nowMs);
    }  // anything else, such as claude-cube frames, is ignored
}

void pollSerial(uint32_t nowMs) {
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        if (c == '\r') continue;
        if (c != '\n') {
            if (commandLen < COMMAND_MAX - 1) {
                command[commandLen++] = c;
            } else {
                commandOverflow = true;
            }
            continue;
        }
        command[commandLen] = '\0';
        if (!commandOverflow) handleCommand(command, nowMs);
        commandLen = 0;
        commandOverflow = false;
    }
}

buddy::MoodInputs gatherInputs(const TouchInput& touch) {
    buddy::MoodInputs in;
    in.shake = lastMotion.shake;
    in.slap = lastMotion.slaps != seenSlaps;
    in.tap = lastMotion.taps != seenTaps;
    in.poke = touch.poke;
    in.stroking = touch.stroking;
    in.handNear = proximity.near();
    in.tilted = lastMotion.tilted;
    seenSlaps = lastMotion.slaps;
    seenTaps = lastMotion.taps;
    return in;
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 0;  // set up below with a larger receive buffer
    cfg.internal_mic = false;  // mic and speaker share I2S; only the speaker is used
    M5.begin(cfg);
    Serial.setRxBufferSize(RX_BUFFER_BYTES);
    Serial.begin(115200);
    M5.Speaker.setVolume(SPEAKER_VOLUME);
    M5.Display.setBrightness(theme::BRIGHTNESS_AWAKE);

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(M5.Display.width(), M5.Display.height());

    i2cLock = xSemaphoreCreateMutex();
    proximity.begin();
    motion.begin(i2cLock);
}

void loop() {
    uint32_t frameStartMs = millis();
    TouchInput touch = readSharedBus(frameStartMs);
    lastMotion = motion.snapshot();
    if (moodEngine.update(gatherInputs(touch), frameStartMs)) {
        onMood(moodEngine.mood(), frameStartMs);
    }
    eyes.update(lastMotion, frameStartMs);
    eyes.draw(canvas, frameStartMs);
    canvas.pushSprite(0, 0);
    voice.service(millis());
    pollSerial(millis());

    uint32_t spent = millis() - frameStartMs;
    framesPerSecond += 0.1f * (1000.0f / fmaxf(spent, 1.0f) - framesPerSecond);
    delay(spent < FRAME_MS ? FRAME_MS - spent : 1);
}
