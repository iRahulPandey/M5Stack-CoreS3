// Balance Ball: tilt the board to keep a ball in the middle of the screen. It is happy there, grows
// worried as a wall gets close, and turns red and angry when it hits one. No sound.
// Tap the screen to call the way you hold the board level; that also puts the ball back in the middle.
// Serial commands, one per line: status, snap (screenshot), level, tilt X Y [MS] (lean it from the Mac),
// place X Y [MS] (hold the ball still at a spot), reset.
#include <M5Unified.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "face.h"
#include "level.h"
#include "mood.h"
#include "motionTask.h"
#include "physics.h"
#include "table.h"
#include "theme.h"

namespace {

constexpr uint32_t FRAME_MS = 20;            // the screen takes ~30 ms per frame, so this means "flat out"
constexpr float MAX_STEP_S = 0.1f;
constexpr uint32_t SETTLE_BOOT_MS = 700;     // let the readings settle before taking level at power-up
constexpr uint32_t NOTE_MS = 1500;
constexpr uint32_t HINT_MS = 6000;
constexpr uint32_t DEFAULT_TILT_MS = 3000;
constexpr uint32_t DEFAULT_PLACE_MS = 5000;
constexpr float LOOK_FULL_PX = 40.0f;        // this far off the middle, it looks straight back at it
constexpr size_t COMMAND_MAX = 40;
constexpr size_t RX_BUFFER_BYTES = 1024;
constexpr size_t SNAP_CHUNK_BYTES = 4096;
constexpr uint32_t SNAP_STALL_MS = 1000;

M5Canvas canvas(&M5.Display);
SemaphoreHandle_t i2cLock;
MotionTask motion;
ball::Level level;
const ball::Arena arena;
ball::Ball theBall = ball::centered(arena);
ball::MoodEngine mood;

Reading lastReading;
ball::Vec2 lastPull;
uint32_t lastFrameMs = 0;
uint32_t tiltUntilMs = 0;  // a lean sent over serial, for screenshots and tests without hands
ball::Vec2 tiltFromMac;
uint32_t holdUntilMs = 0;  // `place` holds the ball still until then
const char* note = nullptr;
uint32_t noteUntilMs = 0;
float framesPerSecond = 0.0f;

char command[COMMAND_MAX];
size_t commandLen = 0;
bool commandOverflow = false;

void showNote(const char* text, uint32_t forMs, uint32_t nowMs) {
    note = text;
    noteUntilMs = nowMs + forMs;
}

void setLevelHere(uint32_t nowMs) {
    lastReading = motion.snapshot();
    level.set(lastReading.x, lastReading.y, lastReading.z);
    theBall = ball::centered(arena);
    holdUntilMs = tiltUntilMs = 0;
    showNote("level set", NOTE_MS, nowMs);
}

// Touch shares the internal I2C bus with the IMU task.
bool tapped() {
    if (xSemaphoreTake(i2cLock, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    M5.update();
    bool tap = M5.Touch.getCount() > 0 && M5.Touch.getDetail().wasClicked();
    xSemaphoreGive(i2cLock);
    return tap;
}

BallView viewOf(uint8_t walls, uint32_t nowMs) {
    BallView v;
    v.x = theBall.x;
    v.y = theBall.y;
    v.radius = arena.radius;
    v.walls = walls;
    float dx = arena.width / 2 - theBall.x, dy = arena.height / 2 - theBall.y;
    float scale = 1.0f / fmaxf(hypotf(dx, dy), LOOK_FULL_PX);
    v.lookX = dx * scale;
    v.lookY = dy * scale;
    v.mood = mood.mood();
    v.moodForMs = nowMs - mood.moodSinceMs();
    v.redness = mood.redness();
    return v;
}

void step(uint32_t nowMs) {
    float dtS = fminf((nowMs - lastFrameMs) / 1000.0f, MAX_STEP_S);
    lastFrameMs = nowMs;
    lastReading = motion.snapshot();
    lastPull = nowMs < tiltUntilMs ? tiltFromMac : level.pull(lastReading.x, lastReading.y, lastReading.z);
    if (nowMs >= holdUntilMs) theBall = ball::roll(theBall, arena, lastPull, dtS);
    ball::Situation now;
    now.fromCenterPx = ball::fromCenter(theBall, arena);
    now.wallGapPx = ball::wallGap(theBall, arena);
    now.touching = ball::wallsTouched(theBall, arena) != 0;
    mood.update(now, nowMs);
}

void draw(uint32_t nowMs) {
    uint8_t walls = ball::wallsTouched(theBall, arena);
    bool home = ball::fromCenter(theBall, arena) <= ball::MIDDLE_PX;
    drawTable(canvas, arena, home, walls, nowMs);
    drawBall(canvas, viewOf(walls, nowMs), nowMs);
    drawHud(canvas, mood.streakMs(nowMs), mood.bestMs(), nowMs < noteUntilMs ? note : nullptr);
    canvas.pushSprite(0, 0);
}

void printStatus(uint32_t nowMs) {
    Serial.printf(
        "STATUS {\"mood\":\"%s\",\"redness\":%.2f,\"ball\":[%.1f,%.1f],\"speed\":[%.1f,%.1f],\"walls\":%u,"
        "\"fromCenter\":%.1f,\"wallGap\":%.1f,\"pull\":[%.3f,%.3f],\"reading\":[%.3f,%.3f,%.3f],"
        "\"streakMs\":%lu,\"bestMs\":%lu,\"bumps\":%lu,\"held\":%s,\"fps\":%.1f}\n",
        ball::moodName(mood.mood()), mood.redness(), theBall.x, theBall.y, theBall.vx, theBall.vy,
        ball::wallsTouched(theBall, arena), ball::fromCenter(theBall, arena), ball::wallGap(theBall, arena),
        lastPull.x, lastPull.y, lastReading.x, lastReading.y, lastReading.z,
        static_cast<unsigned long>(mood.streakMs(nowMs)), static_cast<unsigned long>(mood.bestMs()),
        static_cast<unsigned long>(mood.bumps()), nowMs < holdUntilMs ? "true" : "false", framesPerSecond);
}

void sendSnapshot() {
    Serial.printf("SNAP %d %d %d\n", canvas.width(), canvas.height(), static_cast<int>(mood.mood()));
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

// Reads up to `max` numbers separated by spaces; returns how many there were.
int readNumbers(const char* text, float* out, int max) {
    int count = 0;
    char* end = nullptr;
    while (count < max) {
        float value = strtof(text, &end);
        if (end == text) break;
        out[count++] = value;
        text = end;
    }
    return count;
}

void handleCommand(const char* cmd, uint32_t nowMs) {
    float n[3];
    if (strcmp(cmd, "status") == 0) {
        printStatus(nowMs);
    } else if (strcmp(cmd, "snap") == 0) {
        sendSnapshot();
    } else if (strcmp(cmd, "level") == 0) {
        setLevelHere(nowMs);
    } else if (strcmp(cmd, "reset") == 0) {
        theBall = ball::centered(arena);
        holdUntilMs = tiltUntilMs = 0;
    } else if (strncmp(cmd, "tilt ", 5) == 0 && readNumbers(cmd + 5, n, 3) >= 2) {
        tiltFromMac.x = n[0];
        tiltFromMac.y = n[1];
        tiltUntilMs = nowMs + (readNumbers(cmd + 5, n, 3) == 3 ? static_cast<uint32_t>(n[2]) : DEFAULT_TILT_MS);
    } else if (strncmp(cmd, "place ", 6) == 0 && readNumbers(cmd + 6, n, 3) >= 2) {
        theBall.x = fminf(fmaxf(n[0], arena.radius), arena.width - arena.radius);
        theBall.y = fminf(fmaxf(n[1], arena.radius), arena.height - arena.radius);
        theBall.vx = theBall.vy = 0.0f;
        holdUntilMs = nowMs + (readNumbers(cmd + 6, n, 3) == 3 ? static_cast<uint32_t>(n[2]) : DEFAULT_PLACE_MS);
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

void splash() {
    canvas.fillScreen(theme::FLOOR);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    canvas.setTextDatum(middle_center);
    canvas.setTextColor(theme::BODY);
    canvas.drawString("Balance Ball", canvas.width() / 2, canvas.height() / 2 - 14);
    canvas.setFont(&fonts::Font2);
    canvas.setTextColor(theme::TEXT_DIM);
    canvas.drawString("hold me how you like...", canvas.width() / 2, canvas.height() / 2 + 18);
    canvas.pushSprite(0, 0);
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 0;   // set up below with a larger receive buffer
    cfg.internal_spk = false;  // no sound at all
    cfg.internal_mic = false;
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
    splash();

    i2cLock = xSemaphoreCreateMutex();
    motion.begin(i2cLock);
    delay(SETTLE_BOOT_MS);
    uint32_t nowMs = millis();
    setLevelHere(nowMs);
    showNote("tap the screen to set level", HINT_MS, nowMs);
    lastFrameMs = nowMs;
}

void loop() {
    uint32_t frameStartMs = millis();
    if (tapped()) setLevelHere(frameStartMs);
    step(frameStartMs);
    draw(frameStartMs);
    pollSerial(millis());

    uint32_t spent = millis() - frameStartMs;
    framesPerSecond += 0.1f * (1000.0f / fmaxf(spent, 1.0f) - framesPerSecond);
    delay(spent < FRAME_MS ? FRAME_MS - spent : 1);
}
