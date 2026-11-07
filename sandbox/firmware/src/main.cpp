// Sandbox: draw sand, water and walls with your finger; gravity follows the way the board is held.
// Stand it up and the sand falls to the bottom of the screen, turn it over and it falls the other way,
// lay it flat and everything stays put. Shake it to stir everything up. No sound.
// Serial commands, one per line, in cells (160 x 106, 0 0 at the top left of the box): status, snap,
// clear, scene, tool NAME, paint MATERIAL X Y R [DENSITY], line MATERIAL X0 Y0 X1 Y1 R,
// gravity DX DY [MS] (pretend the board is held that way), shake [MS].
#include <M5Unified.h>
#include <esp_memory_utils.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "motionTask.h"
#include "simTask.h"
#include "theme.h"
#include "view.h"
#include "world.h"

using sand::Material;

namespace {

constexpr int WORLD_W = 320 / CELL_PX;
constexpr int WORLD_H = (240 - TOOLBAR_PX) / CELL_PX;
constexpr uint32_t SEED = 0x2545F491;
constexpr uint32_t FRAME_MS = 20;  // the screen takes ~30 ms per frame, so this means "flat out"
constexpr unsigned long DEFAULT_PRETEND_MS = 5000;
constexpr size_t COMMAND_MAX = 48;
constexpr size_t RX_BUFFER_BYTES = 1024;
constexpr size_t SNAP_CHUNK_BYTES = 4096;
constexpr uint32_t SNAP_STALL_MS = 1000;

struct Brush {
    Material material;
    int radius;   // in cells
    int density;  // percent: loose grains are sprinkled, so they look like grains and pour
};

struct Touch {
    bool down = false;
    bool began = false;
    int x = 0, y = 0;
};

M5Canvas canvas(&M5.Display);
SemaphoreHandle_t i2cLock;
MotionTask motion;
SimTask sim;
sand::World world(WORLD_W, WORLD_H, SEED);

Tool tool = Tool::Sand;
Touch lastTouch;
bool stroking = false;  // the finger is drawing in the box: join this frame's point to the last one
int lastCellX = 0, lastCellY = 0;
float framesPerSecond = 0.0f;
float drawMs = 0.0f, pushMs = 0.0f;  // where a frame's time goes

char command[COMMAND_MAX];
size_t commandLen = 0;
bool commandOverflow = false;

Brush brushFor(Tool t) {
    switch (t) {
        case Tool::Sand: return Brush{Material::Sand, 3, 35};
        case Tool::Water: return Brush{Material::Water, 3, 45};
        case Tool::Wall: return Brush{Material::Wall, 2, 100};
        case Tool::Erase: break;
    }
    return Brush{Material::Empty, 5, 100};
}

// Two ramps, a cup, and some sand and water waiting at the top: something to tip over straight away.
void starterScene(sand::World& w) {
    w.clear();
    w.paintLine(14, 34, 64, 50, 1, Material::Wall);
    w.paintLine(146, 52, 96, 68, 1, Material::Wall);
    w.paintLine(58, 84, 58, 100, 1, Material::Wall);
    w.paintLine(58, 100, 102, 100, 1, Material::Wall);
    w.paintLine(102, 100, 102, 84, 1, Material::Wall);
    w.paint(34, 14, 11, Material::Sand, 80);
    w.paint(124, 22, 10, Material::Water, 80);
}

Edit editOf(Edit::Kind kind) {
    Edit e;
    e.kind = kind;
    return e;
}

Edit stroke(Material material, int x0, int y0, int x1, int y1, int radius, int density) {
    Edit e;
    e.kind = x0 == x1 && y0 == y1 ? Edit::Kind::Paint : Edit::Kind::Line;
    e.material = material;
    e.x0 = static_cast<int16_t>(x0);
    e.y0 = static_cast<int16_t>(y0);
    e.x1 = static_cast<int16_t>(x1);
    e.y1 = static_cast<int16_t>(y1);
    e.radius = static_cast<uint8_t>(radius);
    e.density = static_cast<uint8_t>(density);
    return e;
}

// Touch shares the internal I2C bus with the IMU task; if the bus is busy, keep the last reading.
Touch readTouch() {
    if (xSemaphoreTake(i2cLock, pdMS_TO_TICKS(5)) != pdTRUE) return lastTouch;
    M5.update();
    Touch t;
    if (M5.Touch.getCount() > 0) {
        auto d = M5.Touch.getDetail();
        t.down = d.isPressed();
        t.began = d.wasPressed();
        t.x = d.x;
        t.y = d.y;
    }
    xSemaphoreGive(i2cLock);
    lastTouch = t;
    return t;
}

void pressButton(int button) {
    if (button == CLEAR_BUTTON) {
        sim.edit(editOf(Edit::Kind::Clear));
    } else {
        tool = static_cast<Tool>(button);
    }
}

void handleTouch(const Touch& t) {
    if (!t.down) {
        stroking = false;
        return;
    }
    if (t.y < TOOLBAR_PX) {
        if (t.began) pressButton(toolbarButtonAt(t.x, canvas.width()));
        stroking = false;
        return;
    }
    int x = t.x / CELL_PX, y = (t.y - TOOLBAR_PX) / CELL_PX;
    Brush b = brushFor(tool);
    // held still, it paints the same spot every frame, so sand keeps pouring from there
    int fromX = stroking ? lastCellX : x, fromY = stroking ? lastCellY : y;
    sim.edit(stroke(b.material, fromX, fromY, x, y, b.radius, b.density));
    stroking = true;
    lastCellX = x;
    lastCellY = y;
}

void printStatus() {
    Motion m = motion.snapshot();
    SimStats s = sim.stats();
    int counts[4] = {0, 0, 0, 0};
    const uint8_t* cells = sim.lockFrame();
    for (int i = 0; i < sim.frameWidth() * sim.frameHeight(); i++) counts[static_cast<int>(sand::World::materialOf(cells[i]))]++;
    sim.unlockFrame();
    Serial.printf(
        "STATUS {\"tool\":\"%s\",\"down\":[%d,%d],\"strength\":%.2f,\"shaking\":%s,\"shakeEnergy\":%.2f,"
        "\"sand\":%d,\"water\":%d,\"wall\":%d,\"reading\":[%.3f,%.3f,%.3f],\"steps\":%lu,\"fromMac\":%s,"
        "\"gridInPsram\":%s,\"fps\":%.1f,\"ms\":{\"step\":%.2f,\"draw\":%.1f,\"push\":%.1f}}\n",
        toolName(tool), s.gravity.down.dx, s.gravity.down.dy, s.gravity.strength, s.shaking ? "true" : "false",
        m.shakeEnergy, counts[1], counts[2], counts[3], m.x, m.y, m.z, s.steps, s.pretending ? "true" : "false",
        esp_ptr_external_ram(world.cells()) ? "true" : "false", framesPerSecond, s.stepMs, drawMs, pushMs);
}

void sendSnapshot() {
    Serial.printf("SNAP %d %d 0\n", canvas.width(), canvas.height());
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

bool materialFromName(const char* name, Material& out) {
    const char* const names[] = {"empty", "sand", "water", "wall"};
    for (int i = 0; i < 4; i++) {
        if (strcmp(name, names[i]) == 0) {
            out = static_cast<Material>(i);
            return true;
        }
    }
    return false;
}

bool toolFromName(const char* name, Tool& out) {
    for (int i = 0; i < CLEAR_BUTTON; i++) {
        if (strcmp(name, toolName(static_cast<Tool>(i))) == 0) {
            out = static_cast<Tool>(i);
            return true;
        }
    }
    return false;
}

// The commands that change the world: clear, scene, paint and line.
void handleWorldCommand(const char* cmd) {
    char name[8];
    int x0, y0, x1, y1, r, density = 100;
    Material m;
    if (strcmp(cmd, "clear") == 0) {
        sim.edit(editOf(Edit::Kind::Clear));
    } else if (strcmp(cmd, "scene") == 0) {
        sim.edit(editOf(Edit::Kind::Scene));
    } else if (sscanf(cmd, "paint %7s %d %d %d %d", name, &x0, &y0, &r, &density) >= 4 && materialFromName(name, m)) {
        sim.edit(stroke(m, x0, y0, x0, y0, r, density));
    } else if (sscanf(cmd, "line %7s %d %d %d %d %d", name, &x0, &y0, &x1, &y1, &r) == 6 && materialFromName(name, m)) {
        sim.edit(stroke(m, x0, y0, x1, y1, r, 100));
    }  // anything else, such as claude-cube frames, is ignored
}

void handleCommand(const char* cmd, uint32_t nowMs) {
    char name[8];
    int dx, dy;
    unsigned long ms = DEFAULT_PRETEND_MS;
    if (strcmp(cmd, "status") == 0) {
        printStatus();
    } else if (strcmp(cmd, "snap") == 0) {
        sendSnapshot();
    } else if (sscanf(cmd, "tool %7s", name) == 1) {
        toolFromName(name, tool);
    } else if (sscanf(cmd, "gravity %d %d %lu", &dx, &dy, &ms) >= 2) {
        sim.pretendGravity(sand::Direction{static_cast<int8_t>(dx), static_cast<int8_t>(dy)}, nowMs + ms);
    } else if (strncmp(cmd, "shake", 5) == 0) {
        sscanf(cmd, "shake %lu", &ms);
        sim.pretendShake(nowMs + ms);
    } else {
        handleWorldCommand(cmd);
    }
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

void smooth(float& averageMs, uint32_t us) {
    averageMs += 0.1f * (us / 1000.0f - averageMs);
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

    starterScene(world);
    i2cLock = xSemaphoreCreateMutex();
    motion.begin(i2cLock);
    sim.begin(world, motion, starterScene);  // from here on only the sim task touches `world`
}

// Core 1: touch, drawing and the push to the screen. The sand itself moves on core 0 (SimTask).
void loop() {
    uint32_t frameStartMs = millis();
    Touch touch = readTouch();
    handleTouch(touch);
    uint32_t t0 = micros();
    const uint8_t* cells = sim.lockFrame();
    drawWorld(canvas, cells, sim.frameWidth(), sim.frameHeight(), frameStartMs);
    sim.unlockFrame();
    drawToolbar(canvas, tool);
    uint32_t t1 = micros();
    canvas.pushSprite(0, 0);
    uint32_t t2 = micros();
    pollSerial(millis());
    smooth(drawMs, t1 - t0);
    smooth(pushMs, t2 - t1);

    uint32_t spent = millis() - frameStartMs;
    framesPerSecond += 0.1f * (1000.0f / fmaxf(spent, 1.0f) - framesPerSecond);
    delay(spent < FRAME_MS ? FRAME_MS - spent : 1);
}
