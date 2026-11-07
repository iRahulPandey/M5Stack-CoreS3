// Watching Paint Dry: a freshly painted wall that dries, in real time, over three hours. That's it.
// Touch it while it is wet and your fingerprint stays until the next coat; once it is dry, tap for
// another coat in another thrilling shade. Progress is saved, so unplugging it undoes no drying. No sound.
// Serial commands, one per line: status, snap, at MINUTES (jump to that point of this coat),
// speed N (N times real time, for demos), touch X Y, coat N, reset, untouchable MS (ignore the touchscreen
// for a while, so screenshots show only what the script did).
#include <M5Unified.h>
#include <Preferences.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "theme.h"
#include "view.h"
#include "wall.h"
#include "words.h"

namespace {

constexpr uint32_t SEED = 0x9E3779B9;
constexpr uint32_t FRAME_MS = 100;        // ten frames a second is plenty for paint
constexpr float MAX_STEP_S = 1.0f;
constexpr uint32_t SAVE_EVERY_MS = 60000;
constexpr uint32_t REPLY_MS = 4000;
constexpr uint32_t SHOUT_MS = 1500;
constexpr const char* STORE = "paintdry";
constexpr size_t COMMAND_MAX = 40;
constexpr size_t RX_BUFFER_BYTES = 1024;
constexpr size_t SNAP_CHUNK_BYTES = 4096;
constexpr uint32_t SNAP_STALL_MS = 1000;

M5Canvas canvas(&M5.Display);
paint::Wall wall(320, 240, SEED);
Preferences saved;

float speed = 1.0f;
const char* reply = nullptr;
uint32_t replyUntilMs = 0;
uint32_t shoutUntilMs = 0;
uint32_t untouchableUntilMs = 0;
uint32_t lastFrameMs = 0;
uint32_t lastSaveMs = 0;
float framesPerSecond = 0.0f;

char command[COMMAND_MAX];
size_t commandLen = 0;
bool commandOverflow = false;

void save(uint32_t nowMs) {
    saved.putInt("coat", wall.coat());
    saved.putFloat("elapsed", wall.elapsedS());
    lastSaveMs = nowMs;
}

void touchWall(int x, int y, uint32_t nowMs) {
    paint::Touched result = wall.touch(x, y);
    reply = paint::touchReply(result);
    replyUntilMs = nowMs + REPLY_MS;
    if (result == paint::Touched::Smudged) shoutUntilMs = nowMs + SHOUT_MS;
    if (result == paint::Touched::Recoated) save(nowMs);
}

void handleTouch(uint32_t nowMs) {
    M5.update();
    if (M5.Touch.getCount() == 0) return;
    auto d = M5.Touch.getDetail();
    if (d.wasPressed() && nowMs >= untouchableUntilMs) touchWall(d.x, d.y, nowMs);
}

void printStatus() {
    Serial.printf(
        "STATUS {\"coat\":%d,\"colour\":\"%s\",\"stage\":\"%s\",\"dryness\":%.3f,\"elapsedS\":%.0f,"
        "\"remainingS\":%.0f,\"remark\":\"%s\",\"prints\":%u,\"drips\":%u,\"speed\":%.0f,\"fps\":%.1f}\n",
        wall.coat(), theme::coatColour(wall.coat()).name, paint::stageName(wall.stage()), wall.dryness(),
        wall.elapsedS(), wall.remainingS(), paint::remark(wall.stage(), wall.elapsedS()),
        static_cast<unsigned>(wall.smudges().size()), static_cast<unsigned>(wall.drips().size()), speed,
        framesPerSecond);
}

void sendSnapshot() {
    Serial.printf("SNAP %d %d %d\n", canvas.width(), canvas.height(), static_cast<int>(wall.stage()));
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
    float value;
    int x, y, coat;
    if (strcmp(cmd, "status") == 0) {
        printStatus();
    } else if (strcmp(cmd, "snap") == 0) {
        sendSnapshot();
    } else if (sscanf(cmd, "at %f", &value) == 1) {
        wall.jumpTo(value * 60.0f);
        save(nowMs);
    } else if (sscanf(cmd, "speed %f", &value) == 1 && value > 0.0f) {
        speed = value;
    } else if (sscanf(cmd, "touch %d %d", &x, &y) == 2) {
        touchWall(x, y, nowMs);
    } else if (sscanf(cmd, "coat %d", &coat) == 1 && coat >= 1) {
        wall.restore(coat, 0.0f);
        save(nowMs);
    } else if (sscanf(cmd, "untouchable %d", &x) == 1) {
        untouchableUntilMs = nowMs + static_cast<uint32_t>(x > 0 ? x : 0);
    } else if (strcmp(cmd, "reset") == 0) {
        wall.restore(1, 0.0f);
        speed = 1.0f;
        save(nowMs);
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
    cfg.internal_spk = false;  // no sound at all
    cfg.internal_mic = false;
    cfg.internal_imu = false;  // nothing here moves
    M5.begin(cfg);
    Serial.setRxBufferSize(RX_BUFFER_BYTES);
    Serial.begin(115200);
    M5.Display.setBrightness(theme::BRIGHTNESS);

    // The 150 KB frame fits in internal RAM, where drawing and pushing it are much faster than in PSRAM.
    canvas.setColorDepth(16);
    canvas.setPsram(false);
    if (canvas.createSprite(M5.Display.width(), M5.Display.height()) == nullptr) {
        canvas.setPsram(true);
        canvas.createSprite(M5.Display.width(), M5.Display.height());
    }

    saved.begin(STORE, false);
    wall.restore(saved.getInt("coat", 1), saved.getFloat("elapsed", 0.0f));
    lastFrameMs = lastSaveMs = millis();
}

void loop() {
    uint32_t frameStartMs = millis();
    wall.advance(fminf((frameStartMs - lastFrameMs) / 1000.0f, MAX_STEP_S) * speed);
    lastFrameMs = frameStartMs;
    handleTouch(frameStartMs);
    if (frameStartMs - lastSaveMs >= SAVE_EVERY_MS) save(frameStartMs);

    Caption caption;
    caption.reply = frameStartMs < replyUntilMs ? reply : nullptr;
    caption.signShouting = frameStartMs < shoutUntilMs;
    drawWall(canvas, wall, caption, frameStartMs);
    canvas.pushSprite(0, 0);
    pollSerial(millis());

    uint32_t spent = millis() - frameStartMs;
    framesPerSecond += 0.1f * (1000.0f / fmaxf(spent, 1.0f) - framesPerSecond);
    delay(spent < FRAME_MS ? FRAME_MS - spent : 1);
}
