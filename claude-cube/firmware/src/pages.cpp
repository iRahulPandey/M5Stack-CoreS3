#include "pages.h"

#include <stdio.h>
#include <string.h>

#include "format.h"
#include "theme.h"

namespace {

using namespace theme;

constexpr const char* PAGE_NAMES[] = {"TOKENS", "MODEL", "CONTEXT", "NOW", "COST"};
constexpr int HEADER_Y = 8;
constexpr int VALUE_Y = 34;
constexpr int ROWS_Y = 104;
constexpr int ROW_H = 24;
constexpr int DOTS_Y = 226;
constexpr int HEADER_GAP = 16;  // between the page label and the session title
constexpr float CONTEXT_WARN = 0.80f;
constexpr uint32_t MIN_RATE_ELAPSED_S = 300;

const lgfx::IFont* const FONT_SMALL = &fonts::FreeMono9pt7b;
const lgfx::IFont* const FONT_TEXT = &fonts::FreeSans9pt7b;
const lgfx::IFont* const FONT_MID = &fonts::FreeSansBold18pt7b;
const lgfx::IFont* const FONT_BIG = &fonts::FreeSansBold24pt7b;

struct StateLook {
    const char* label;
    uint16_t color;
};

// Copies `text` into `out`, shortened with ".." until it fits `maxWidth` in the current font.
void fitText(M5Canvas& c, const char* text, int maxWidth, char* out, size_t size) {
    strlcpy(out, text, size);
    size_t len = strlen(out);
    while (len > 0 && c.textWidth(out) > maxWidth) {
        out[--len] = '\0';
        if (len >= 2) {
            out[len - 2] = '.';
            out[len - 1] = '.';
        }
    }
}

uint32_t ticking(uint32_t base, const SessionState& s, bool linkUp, uint32_t nowMs) {
    return linkUp ? base + (nowMs - s.receivedAtMs) / 1000 : base;
}

void drawHeader(M5Canvas& c, Page page, const SessionState& s, bool linkUp) {
    c.setFont(FONT_SMALL);
    c.setTextColor(STEAM);
    c.setTextDatum(top_left);
    const char* label = PAGE_NAMES[static_cast<int>(page)];
    c.drawString(label, MARGIN, HEADER_Y);
    constexpr int titleRight = SCREEN_W - MARGIN - 12;  // leaves room for the link dot
    int titleWidth = titleRight - (MARGIN + c.textWidth(label) + HEADER_GAP);
    char title[49];
    fitText(c, s.title[0] ? s.title : "claude cube", titleWidth, title, sizeof(title));
    c.setTextDatum(top_right);
    c.drawString(title, titleRight, HEADER_Y);
    c.fillCircle(SCREEN_W - MARGIN - 3, HEADER_Y + 6, 3, linkUp ? CREMA : GROUNDS);
}

void drawDots(M5Canvas& c, Page page) {
    constexpr int count = static_cast<int>(Page::Count);
    constexpr int gap = 12;
    int x = SCREEN_W / 2 - (count - 1) * gap / 2;
    for (int i = 0; i < count; i++, x += gap) {
        c.fillCircle(x, DOTS_Y, 3, i == static_cast<int>(page) ? CREMA : GROUNDS);
    }
}

void drawBig(M5Canvas& c, const char* text, uint16_t color, const lgfx::IFont* font = FONT_BIG) {
    char fitted[49];
    c.setFont(font);
    c.setTextColor(color);
    c.setTextDatum(top_left);
    fitText(c, text, SCREEN_W - 2 * MARGIN, fitted, sizeof(fitted));
    c.drawString(fitted, MARGIN, VALUE_Y);
}

void drawRow(M5Canvas& c, int index, const char* label, const char* value) {
    int y = ROWS_Y + index * ROW_H;
    c.setFont(FONT_SMALL);
    c.setTextDatum(top_left);
    c.setTextColor(STEAM);
    c.drawString(label, MARGIN, y);
    c.setTextDatum(top_right);
    c.setTextColor(FOAM);
    c.drawString(value, SCREEN_W - MARGIN, y);
}

void drawTokens(M5Canvas& c, const SessionState& s) {
    char buf[16];
    uint64_t total = s.tokensIn + s.tokensOut + s.tokensCacheRead + s.tokensCacheWrite;
    formatCount(total, buf, sizeof(buf));
    drawBig(c, s.valid ? buf : "--", CREMA);
    formatCount(s.tokensOut, buf, sizeof(buf));
    drawRow(c, 0, "output", buf);
    formatCount(s.tokensIn, buf, sizeof(buf));
    drawRow(c, 1, "input", buf);
    formatCount(s.tokensCacheRead, buf, sizeof(buf));
    drawRow(c, 2, "cache read", buf);
    formatCount(s.tokensCacheWrite, buf, sizeof(buf));
    drawRow(c, 3, "cache write", buf);
}

void drawModel(M5Canvas& c, const SessionState& s) {
    drawBig(c, s.model[0] ? s.model : "--", FOAM, FONT_MID);
    drawRow(c, 0, "id", s.modelId[0] ? s.modelId : "--");
    drawRow(c, 1, "effort", s.effort[0] ? s.effort : "--");
    drawRow(c, 2, "fast mode", s.fast ? "on" : "off");
}

void drawContext(M5Canvas& c, const SessionState& s) {
    float used = s.contextMax ? static_cast<float>(s.contextUsed) / static_cast<float>(s.contextMax) : 0.0f;
    uint16_t color = used >= CONTEXT_WARN ? CHERRY : CREMA;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(used * 100.0f + 0.5f));
    drawBig(c, s.valid ? buf : "--", color);

    constexpr int barY = 84, barH = 14, barW = SCREEN_W - 2 * MARGIN;
    c.fillRoundRect(MARGIN, barY, barW, barH, 3, SAUCER);
    c.drawRoundRect(MARGIN, barY, barW, barH, 3, GROUNDS);
    int fill = static_cast<int>(barW * (used > 1.0f ? 1.0f : used));
    if (fill > 0) {
        c.fillRoundRect(MARGIN, barY, fill, barH, 3, color);
    }
    formatCount(s.contextUsed, buf, sizeof(buf));
    drawRow(c, 0, "used", buf);
    formatCount(s.contextMax, buf, sizeof(buf));
    drawRow(c, 1, "window", buf);
    formatCount(s.contextMax > s.contextUsed ? s.contextMax - s.contextUsed : 0, buf, sizeof(buf));
    drawRow(c, 2, "left", buf);
}

StateLook lookFor(const SessionState& s, bool linkUp, char* scratch, size_t size) {
    if (!linkUp) return {"Waiting for Mac", STEAM};
    if (!strcmp(s.state, "thinking")) return {"Thinking", COLDBREW};
    if (!strcmp(s.state, "waiting")) return {"Needs you", CHERRY};
    if (!strcmp(s.state, "done")) return {"Your turn", CREMA};
    if (!strcmp(s.state, "error")) return {"Stopped: error", CHERRY};
    if (!strcmp(s.state, "ended")) return {"Session ended", STEAM};
    if (!strcmp(s.state, "tool")) {
        snprintf(scratch, size, "Running %s", s.tool[0] ? s.tool : "tool");
        return {scratch, COLDBREW};
    }
    return {"Idle", STEAM};
}

void drawPill(M5Canvas& c, const char* text, uint32_t seconds) {
    constexpr int y = 84, h = 24, w = SCREEN_W - 2 * MARGIN;
    c.fillRoundRect(MARGIN, y, w, h, h / 2, SAUCER);
    c.drawRoundRect(MARGIN, y, w, h, h / 2, GROUNDS);
    char timer[12];
    snprintf(timer, sizeof(timer), "%lu:%02lu", static_cast<unsigned long>(seconds / 60),
             static_cast<unsigned long>(seconds % 60));
    c.setFont(FONT_TEXT);
    c.setTextColor(STEAM);
    c.setTextDatum(middle_right);
    c.drawString(timer, SCREEN_W - MARGIN - 12, y + h / 2);
    char fitted[41];
    fitText(c, text, w - 80, fitted, sizeof(fitted));
    c.setTextDatum(middle_left);
    c.drawString(fitted, MARGIN + 12, y + h / 2);
}

void drawNow(M5Canvas& c, const SessionState& s, bool linkUp, uint32_t nowMs) {
    char scratch[48];
    StateLook look = lookFor(s, linkUp, scratch, sizeof(scratch));
    drawBig(c, look.label, look.color, FONT_MID);

    const char* pill = s.detail[0] ? s.detail : "";
    if (!strcmp(s.state, "waiting") && !pill[0]) {
        pill = s.tool[0] ? s.tool : "Claude needs your input";
    }
    drawPill(c, linkUp ? pill : "use Claude Code to wake it", ticking(s.stateForS, s, linkUp, nowMs));

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s.turns));
    drawRow(c, 1, "turns", buf);
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(s.toolCalls));
    drawRow(c, 2, "tool calls", buf);
    drawRow(c, 3, "project", s.project[0] ? s.project : "--");
}

void drawCost(M5Canvas& c, const SessionState& s, bool linkUp, uint32_t nowMs) {
    char buf[24];
    formatCost(s.costUsd, buf, sizeof(buf));
    drawBig(c, s.valid ? buf : "--", CREMA);

    uint32_t elapsed = ticking(s.elapsedS, s, linkUp, nowMs);
    formatDuration(elapsed, buf, sizeof(buf));
    drawRow(c, 0, "session time", buf);
    if (s.costUsd >= 0 && elapsed >= MIN_RATE_ELAPSED_S) {
        formatCost(s.costUsd * 3600.0f / static_cast<float>(elapsed), buf, sizeof(buf));
    } else {
        strlcpy(buf, "--", sizeof(buf));
    }
    drawRow(c, 1, "per hour", buf);
    snprintf(buf, sizeof(buf), "+%lu / -%lu", static_cast<unsigned long>(s.linesAdded),
             static_cast<unsigned long>(s.linesRemoved));
    drawRow(c, 2, "lines changed", buf);

    c.setFont(FONT_SMALL);
    c.setTextColor(STEAM);
    c.setTextDatum(top_left);
    c.drawString("API-equivalent estimate", MARGIN, ROWS_Y + 3 * ROW_H + 4);
}

}  // namespace

Page nextPage(Page page) {
    return static_cast<Page>((static_cast<int>(page) + 1) % static_cast<int>(Page::Count));
}

void drawPage(M5Canvas& c, Page page, const SessionState& s, bool linkUp, uint32_t nowMs) {
    c.fillScreen(ROAST);
    if (linkUp && !strcmp(s.state, "waiting")) {
        c.drawRect(0, 0, SCREEN_W, SCREEN_H, CHERRY);
        c.drawRect(1, 1, SCREEN_W - 2, SCREEN_H - 2, CHERRY);
    }
    drawHeader(c, page, s, linkUp);
    switch (page) {
        case Page::Tokens: drawTokens(c, s); break;
        case Page::Model: drawModel(c, s); break;
        case Page::Context: drawContext(c, s); break;
        case Page::Now: drawNow(c, s, linkUp, nowMs); break;
        case Page::Cost: drawCost(c, s, linkUp, nowMs); break;
        case Page::Count: break;
    }
    drawDots(c, page);
}
