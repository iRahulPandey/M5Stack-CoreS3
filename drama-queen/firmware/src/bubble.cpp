#include "bubble.h"

#include <esp_random.h>
#include <string.h>

#include "theme.h"

using namespace theme;

namespace {

constexpr int BOX_X = 8, BOX_Y = 170, BOX_W = 304, BOX_H = 64;
constexpr int TEXT_W = 284;
constexpr uint32_t MS_PER_LETTER = 35;

// Splits `text` at a space so the first line fits `width`; returns the index where line two starts
// (or the full length when everything fits on one line).
size_t splitAt(M5Canvas& c, const char* text, int width) {
    if (c.textWidth(text) <= width) return strlen(text);
    char probe[64];
    size_t best = 0;
    for (size_t i = 0; text[i] && i < sizeof(probe) - 1; i++) {
        if (text[i] != ' ') continue;
        memcpy(probe, text, i);
        probe[i] = '\0';
        if (c.textWidth(probe) > width) break;
        best = i;
    }
    return best ? best : strlen(text);
}

void drawFrame(M5Canvas& c, int ox, int oy) {
    c.fillRoundRect(BOX_X + ox, BOX_Y + oy, BOX_W, BOX_H, 14, INK);
    c.fillRoundRect(BOX_X + 3 + ox, BOX_Y + 3 + oy, BOX_W - 6, BOX_H - 6, 12, PAPER);
    c.fillTriangle(176 + ox, BOX_Y + 1 + oy, 202 + ox, BOX_Y + 1 + oy, 190 + ox, BOX_Y - 12 + oy, INK);
    c.fillTriangle(181 + ox, BOX_Y + 4 + oy, 197 + ox, BOX_Y + 4 + oy, 190 + ox, BOX_Y - 6 + oy, PAPER);
}

}  // namespace

void SpeechBubble::say(const char* text, uint32_t nowMs) {
    strlcpy(_text, text, sizeof(_text));
    _startMs = nowMs;
}

void SpeechBubble::draw(M5Canvas& c, uint32_t nowMs, float jitter) const {
    if (!_text[0]) return;
    int ox = jitter > 0 ? static_cast<int>(esp_random() % 3) - 1 : 0, oy = ox;
    drawFrame(c, ox, oy);

    c.setFont(&fonts::FreeSansBold12pt7b);
    size_t len = strlen(_text);
    size_t split = splitAt(c, _text, TEXT_W);
    if (split < len && c.textWidth(_text + split + 1) > TEXT_W) {  // two lines still too long: smaller type
        c.setFont(&fonts::FreeSansBold9pt7b);
        split = splitAt(c, _text, TEXT_W);
    }
    size_t shown = (nowMs - _startMs) / MS_PER_LETTER + 1;
    char line[64];
    c.setTextColor(INK);
    c.setTextDatum(middle_center);
    bool twoLines = split < len;
    int cy = BOX_Y + BOX_H / 2 + oy, x = 160 + ox;

    size_t first = shown < split ? shown : split;
    memcpy(line, _text, first);
    line[first] = '\0';
    c.drawString(line, x, twoLines ? cy - 13 : cy);
    if (twoLines && shown > split + 1) {
        size_t second = shown - split - 1;
        size_t rest = len - split - 1;
        if (second > rest) second = rest;
        memcpy(line, _text + split + 1, second);
        line[second] = '\0';
        c.drawString(line, x, cy + 13);
    }
}
