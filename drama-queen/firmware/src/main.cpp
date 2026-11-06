// Drama Queen: a cube that overreacts to everything.
// Toss it: "WHEEE!". Tilt it: queasy. Face down: sobs. On its back: faints. Slap it: outraged,
// then furious. Shake it: rattled, then dizzy. Tap the screen: tickled. Stroke it: smitten.
// Leave it alone: bored, then beauty sleep.
// Serial commands, one per line: status, hits (recent jolts and why they counted or not),
// react <name> (act out a reaction now), snap (screenshot).
#include <M5Unified.h>
#include <esp_random.h>
#include <math.h>
#include <string.h>

#include "bubble.h"
#include "drama.h"
#include "face.h"
#include "lines.h"
#include "motionTask.h"
#include "theme.h"
#include "voice.h"

using drama::Reaction;

namespace {

constexpr uint32_t FRAME_MS = 33;
constexpr uint8_t SPEAKER_VOLUME = 150;
constexpr size_t COMMAND_MAX = 40;
constexpr size_t RX_BUFFER_BYTES = 1024;
constexpr size_t SNAP_CHUNK_BYTES = 4096;
constexpr uint32_t SNAP_STALL_MS = 1000;
constexpr size_t HIT_LOG = 16;

struct Hit {
    float peak;
    uint16_t ms;
    drama::HitVerdict verdict;
    uint32_t atMs;
};

M5Canvas canvas(&M5.Display);
SemaphoreHandle_t i2cLock;
MotionTask motion;
drama::DramaEngine engine;
SpeechBubble bubble;
Voice voice;

drama::MotionState lastMotion;
uint32_t seenSlaps = 0, seenTaps = 0, seenPickups = 0, seenLandings = 0, seenHits = 0;
Hit hitLog[HIT_LOG];
size_t hitCount = 0;
uint32_t lastLineMs = 0;
float framesPerSecond = 0.0f;

char command[COMMAND_MAX];
size_t commandLen = 0;
bool commandOverflow = false;

struct TouchInput {
    bool poke = false;
    bool stroking = false;
};

// Touch shares the internal I2C bus with the IMU task.
TouchInput readTouch() {
    TouchInput touch;
    if (xSemaphoreTake(i2cLock, pdMS_TO_TICKS(5)) != pdTRUE) return touch;
    M5.update();
    if (M5.Touch.getCount() > 0) {
        auto d = M5.Touch.getDetail();
        touch.poke = d.wasClicked();
        touch.stroking = d.isDragging() || d.isFlicking();
    }
    xSemaphoreGive(i2cLock);
    return touch;
}

void speak(Reaction reaction, uint32_t nowMs) {
    const char* line = pickLine(reaction, esp_random());
    bubble.say(line, nowMs);
    voice.say(reaction, line, esp_random());
    lastLineMs = nowMs;
}

void onReaction(Reaction reaction, uint32_t nowMs) {
    speak(reaction, nowMs);
    M5.Display.setBrightness(reaction == Reaction::Asleep ? theme::BRIGHTNESS_ASLEEP : theme::BRIGHTNESS_AWAKE);
}

// Some reactions keep talking while they last: idle remarks, more sobbing, more nausea.
void chatter(uint32_t nowMs) {
    uint32_t every = chatterEveryMs(engine.reaction());
    if (every > 0 && nowMs - lastLineMs >= every) speak(engine.reaction(), nowMs);
}

void logHit(uint32_t nowMs) {
    if (lastMotion.hits == seenHits) return;
    seenHits = lastMotion.hits;
    hitLog[hitCount % HIT_LOG] = {lastMotion.lastHitPeak, lastMotion.lastHitMs, lastMotion.lastHitVerdict, nowMs};
    hitCount++;
}

drama::Inputs gatherInputs(const TouchInput& touch) {
    const auto& m = lastMotion;
    drama::Inputs in;
    in.shake = m.shake;
    in.slap = m.slaps != seenSlaps;
    in.tap = m.taps != seenTaps;
    in.pickedUp = m.pickups != seenPickups;
    in.landed = m.landings != seenLandings;
    in.poke = touch.poke;
    in.stroking = touch.stroking;
    in.falling = m.falling;
    in.faceDown = m.faceDown;
    in.onBack = m.onBack;
    in.tilted = m.tilted;
    seenSlaps = m.slaps;
    seenTaps = m.taps;
    seenPickups = m.pickups;
    seenLandings = m.landings;
    return in;
}

void printStatus() {
    const auto& m = lastMotion;
    Serial.printf(
        "STATUS {\"reaction\":\"%s\",\"line\":\"%s\",\"slapStreak\":%u,\"shake\":%u,\"energy\":%.2f,"
        "\"gravity\":[%.2f,%.2f,%.2f],\"faceDown\":%s,\"onBack\":%s,\"tilted\":%s,\"falling\":%s,"
        "\"falls\":%lu,\"landings\":%lu,\"pickups\":%lu,\"slaps\":%lu,\"taps\":%lu,\"hits\":%lu,\"fps\":%.1f}\n",
        drama::reactionName(engine.reaction()), bubble.text(), engine.slapStreak(), static_cast<unsigned>(m.shake),
        m.energy, m.gravityX, m.gravityY, m.gravityZ, m.faceDown ? "true" : "false", m.onBack ? "true" : "false",
        m.tilted ? "true" : "false", m.falling ? "true" : "false", static_cast<unsigned long>(m.falls),
        static_cast<unsigned long>(m.landings), static_cast<unsigned long>(m.pickups),
        static_cast<unsigned long>(m.slaps), static_cast<unsigned long>(m.taps), static_cast<unsigned long>(m.hits),
        framesPerSecond);
}

void printHits(uint32_t nowMs) {
    Serial.print("HITS [");
    size_t first = hitCount > HIT_LOG ? hitCount - HIT_LOG : 0;
    for (size_t i = first; i < hitCount; i++) {
        const Hit& h = hitLog[i % HIT_LOG];
        Serial.printf("%s{\"peak\":%.2f,\"ms\":%u,\"verdict\":\"%s\",\"agoS\":%lu}", i == first ? "" : ",", h.peak,
                      h.ms, drama::hitVerdictName(h.verdict), static_cast<unsigned long>((nowMs - h.atMs) / 1000));
    }
    Serial.print("]\n");
}

void sendSnapshot() {
    Serial.printf("SNAP %d %d %d\n", canvas.width(), canvas.height(), static_cast<int>(engine.reaction()));
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
    Reaction forced;
    if (strcmp(cmd, "status") == 0) {
        printStatus();
    } else if (strcmp(cmd, "hits") == 0) {
        printHits(nowMs);
    } else if (strcmp(cmd, "snap") == 0) {
        sendSnapshot();
    } else if (strncmp(cmd, "react ", 6) == 0 && drama::reactionFromName(cmd + 6, forced)) {
        engine.force(forced, nowMs);
        onReaction(forced, nowMs);
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

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 0;   // set up below with a larger receive buffer
    cfg.internal_mic = false;  // mic and speaker share I2S; only the speaker is used
    M5.begin(cfg);
    Serial.setRxBufferSize(RX_BUFFER_BYTES);
    Serial.begin(115200);
    M5.Speaker.setVolume(SPEAKER_VOLUME);
    M5.Display.setBrightness(theme::BRIGHTNESS_AWAKE);

    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(M5.Display.width(), M5.Display.height());

    voice.begin();
    i2cLock = xSemaphoreCreateMutex();
    motion.begin(i2cLock);
    speak(Reaction::Idle, millis());
}

void loop() {
    uint32_t frameStartMs = millis();
    TouchInput touch = readTouch();
    lastMotion = motion.snapshot();
    logHit(frameStartMs);
    if (engine.update(gatherInputs(touch), frameStartMs)) {
        onReaction(engine.reaction(), frameStartMs);
    } else {
        chatter(frameStartMs);
    }
    Reaction r = engine.reaction();
    bool trembling = r == Reaction::Outraged || r == Reaction::Furious || r == Reaction::Wheee || r == Reaction::Rattled;
    drawFace(canvas, r, frameStartMs, engine.sinceMs(), lastMotion);
    bubble.draw(canvas, frameStartMs, trembling ? 1.0f : 0.0f);
    canvas.pushSprite(0, 0);
    pollSerial(millis());

    uint32_t spent = millis() - frameStartMs;
    framesPerSecond += 0.1f * (1000.0f / fmaxf(spent, 1.0f) - framesPerSecond);
    delay(spent < FRAME_MS ? FRAME_MS - spent : 1);
}
