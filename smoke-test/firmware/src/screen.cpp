#include "screen.h"

#include <stdio.h>
#include <string.h>

#include "report.h"
#include "theme.h"

namespace {

using namespace theme;

constexpr int HEADER_Y = 8;
constexpr int ROWS_Y = 32;
constexpr int ROW_H = 15;
constexpr int ROWS_PER_PAGE = 12;
constexpr int STATUS_X = SCREEN_W - MARGIN;
constexpr int DETAIL_X = 130;
constexpr int DOTS_Y = 228;

const lgfx::IFont* const FONT_SMALL = &fonts::FreeMono9pt7b;
const lgfx::IFont* const FONT_ROW = &fonts::Font2;
const lgfx::IFont* const FONT_BIG = &fonts::FreeSansBold24pt7b;

uint16_t statusColor(Status s) {
    switch (s) {
        case Status::Pass: return CREMA;
        case Status::Warn: return COLDBREW;
        case Status::Fail: return CHERRY;
        case Status::Info: return STEAM;
    }
    return STEAM;
}

void drawHeader(M5Canvas& c, const char* left, const char* right) {
    c.setFont(FONT_SMALL);
    c.setTextColor(STEAM);
    c.setTextDatum(top_left);
    c.drawString(left, MARGIN, HEADER_Y);
    c.setFont(FONT_ROW);  // the narrower font keeps the right side clear of the label
    c.setTextDatum(top_right);
    c.drawString(right, SCREEN_W - MARGIN, HEADER_Y);
}

void drawDots(M5Canvas& c, Screen screen) {
    constexpr int count = static_cast<int>(Screen::Count);
    constexpr int gap = 12;
    int x = SCREEN_W / 2 - (count - 1) * gap / 2;
    for (int i = 0; i < count; i++, x += gap) {
        c.fillCircle(x, DOTS_Y, 3, i == static_cast<int>(screen) ? CREMA : GROUNDS);
    }
}

void drawCheckRow(M5Canvas& c, int row, const CheckResult& r) {
    int y = ROWS_Y + row * ROW_H;
    c.setFont(FONT_ROW);
    c.setTextDatum(top_left);
    c.setTextColor(FOAM);
    c.drawString(r.name, MARGIN, y);
    c.setTextColor(STEAM);
    char detail[44];
    strlcpy(detail, r.detail, sizeof(detail));
    for (size_t len = strlen(detail); len > 0 && c.textWidth(detail) > STATUS_X - 40 - DETAIL_X; len--) {
        detail[len - 1] = '\0';
    }
    c.drawString(detail, DETAIL_X, y);
    c.setTextDatum(top_right);
    c.setTextColor(statusColor(r.status));
    c.drawString(r.status == Status::Pass ? "PASS" : r.status == Status::Warn ? "WARN"
                 : r.status == Status::Fail ? "FAIL" : "INFO", STATUS_X, y);
}

void drawChecks(M5Canvas& c, int first, const CheckList& checks) {
    char summary[40];
    snprintf(summary, sizeof(summary), "%u pass, %u warn, %u fail", static_cast<unsigned>(checks.countOf(Status::Pass)),
             static_cast<unsigned>(checks.countOf(Status::Warn)), static_cast<unsigned>(checks.countOf(Status::Fail)));
    drawHeader(c, "CHECKS", summary);
    for (int row = 0; row < ROWS_PER_PAGE && first + row < static_cast<int>(checks.count); row++) {
        drawCheckRow(c, row, checks.items[first + row]);
    }
}

void drawBenchRow(M5Canvas& c, int row, const char* label, const char* value) {
    int y = 96 + row * 20;
    c.setFont(FONT_ROW);
    c.setTextDatum(top_left);
    c.setTextColor(STEAM);
    c.drawString(label, MARGIN, y);
    c.setTextDatum(top_right);
    c.setTextColor(FOAM);
    c.drawString(value, SCREEN_W - MARGIN, y);
}

void drawMemory(M5Canvas& c, const MemBenchResults& bench) {
    drawHeader(c, "MEMORY", "MB/s, 1 MB = 10^6 bytes");
    E01Ceiling e = e01Ceiling(bench);
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f MB/s", e.bandwidthMBps);
    c.setFont(FONT_BIG);
    c.setTextColor(CREMA);
    c.setTextDatum(top_left);
    c.drawString(buf, MARGIN, 32);
    snprintf(buf, sizeof(buf), "%.1f  /  %.1f", bench.psramRead, bench.psramReadDualCore);
    drawBenchRow(c, 0, "PSRAM read, 1 core / 2 cores", buf);
    snprintf(buf, sizeof(buf), "%.1f", bench.psramWrite);
    drawBenchRow(c, 1, "PSRAM write", buf);
    snprintf(buf, sizeof(buf), "%.1f  /  %.1f", bench.sramRead, bench.flashRead);
    drawBenchRow(c, 2, "SRAM read / flash read", buf);
    snprintf(buf, sizeof(buf), "%.1f tok/s", e.int8_3m3TokS);
    drawBenchRow(c, 3, "E01 ceiling, 3.3M INT8 model", buf);
    snprintf(buf, sizeof(buf), "%.1f tok/s", e.fp32_260kTokS);
    drawBenchRow(c, 4, "E01 ceiling, 260K FP32 model", buf);
}

}  // namespace

Screen nextScreen(Screen screen) {
    return static_cast<Screen>((static_cast<int>(screen) + 1) % static_cast<int>(Screen::Count));
}

void drawRunning(M5Canvas& c) {
    c.fillScreen(ROAST);
    drawHeader(c, "SMOKE TEST", "CoreS3");
    c.setFont(&fonts::FreeSansBold18pt7b);
    c.setTextColor(COLDBREW);
    c.setTextDatum(middle_center);
    c.drawString("Running checks", SCREEN_W / 2, SCREEN_H / 2 - 10);
    c.setFont(FONT_SMALL);
    c.setTextColor(STEAM);
    c.drawString("about 15 seconds, one beep", SCREEN_W / 2, SCREEN_H / 2 + 24);
}

void drawScreen(M5Canvas& c, Screen screen, const CheckList& checks, const MemBenchResults& bench) {
    c.fillScreen(ROAST);
    switch (screen) {
        case Screen::ChecksFirst: drawChecks(c, 0, checks); break;
        case Screen::ChecksSecond: drawChecks(c, ROWS_PER_PAGE, checks); break;
        case Screen::Memory: drawMemory(c, bench); break;
        case Screen::Count: break;
    }
    drawDots(c, screen);
}
