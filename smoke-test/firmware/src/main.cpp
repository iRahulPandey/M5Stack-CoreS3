// CoreS3 smoke test: checks every on-board part, benchmarks memory, and reports over USB serial.
// Serial commands, one per line: report, rerun, next (flip screen), snap (screenshot, as in claude-cube).
#include <M5Unified.h>
#include <string.h>

#include "checks.h"
#include "memBench.h"
#include "report.h"
#include "screen.h"

namespace {

constexpr size_t COMMAND_MAX = 32;
constexpr size_t RX_BUFFER_BYTES = 1024;
constexpr size_t SNAP_CHUNK_BYTES = 4096;
constexpr uint32_t SNAP_STALL_MS = 1000;

M5Canvas canvas(&M5.Display);
CheckList checks;
MemBenchResults bench = {};
uint32_t elapsedMs = 0;
Screen screen = Screen::ChecksFirst;

char command[COMMAND_MAX];
size_t commandLen = 0;
bool commandOverflow = false;

void show() {
    drawScreen(canvas, screen, checks, bench);
    canvas.pushSprite(0, 0);
}

void runAll() {
    drawRunning(canvas);
    canvas.pushSprite(0, 0);
    uint32_t t0 = millis();
    runChecks(checks);
    bench = runMemBench();
    elapsedMs = millis() - t0;
    printReport(checks, bench, elapsedMs);
    screen = Screen::ChecksFirst;
    show();
}

void sendSnapshot() {
    Serial.printf("SNAP %d %d %d\n", canvas.width(), canvas.height(), static_cast<int>(screen));
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

void handleCommand(const char* cmd) {
    if (strcmp(cmd, "report") == 0) {
        printReport(checks, bench, elapsedMs);
    } else if (strcmp(cmd, "rerun") == 0) {
        runAll();
    } else if (strcmp(cmd, "next") == 0) {
        screen = nextScreen(screen);
        show();
    } else if (strcmp(cmd, "snap") == 0) {
        sendSnapshot();
    }  // anything else (for example claude-cube frames) is ignored
}

void pollSerial() {
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
        if (!commandOverflow) handleCommand(command);
        commandLen = 0;
        commandOverflow = false;
    }
}

}  // namespace

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 0;  // set up below with a larger receive buffer
    M5.begin(cfg);
    Serial.setRxBufferSize(RX_BUFFER_BYTES);
    Serial.begin(115200);
    canvas.setPsram(true);
    canvas.setColorDepth(16);
    canvas.createSprite(M5.Display.width(), M5.Display.height());
    runAll();
}

void loop() {
    M5.update();
    pollSerial();
    if (M5.Touch.getCount() > 0 && M5.Touch.getDetail().wasClicked()) {
        screen = nextScreen(screen);
        show();
    }
    delay(10);
}
