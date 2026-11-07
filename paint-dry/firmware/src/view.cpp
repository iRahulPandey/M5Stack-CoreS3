#include "view.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "theme.h"
#include "words.h"

namespace {

constexpr int LEVELS = 64;           // shades of the paint, darkest to brightest
constexpr int MID = LEVELS / 2;      // the paint itself
constexpr float LEVEL_STEP = 0.006f; // brightness change per shade
constexpr int ROLLER_PX = 56;        // the width of one pass of the roller
constexpr float ROLLER_SHADES = 5.0f;
constexpr int SHEEN_CENTER = 210;    // along x * 4/5 + y: a glossy band running down-left
constexpr float SHEEN_WIDTH = 46.0f;
constexpr float SHEEN_SHADES = 16.0f;
constexpr int SHEEN_SPAN = 512;
constexpr int DRIP_SHADE = MID - 7;
constexpr int PRINT_SHADE = MID - 6;
constexpr int SIGN_W = 118, SIGN_H = 30, SIGN_MARGIN = 10, POST_H = 16;
constexpr int CARD_H = 62, CARD_MARGIN = 8;
constexpr float PI_F = 3.14159265f;

// One frame's shades of the paint, both as the sprite buffer stores them (big-endian) and as the
// drawing calls take them.
struct Shades {
    uint16_t raw[LEVELS];
    uint16_t color[LEVELS];
};

uint16_t rgbOf(float r, float g, float b) {
    auto c = [](float v) { return static_cast<uint8_t>(std::min(std::max(v, 0.0f), 255.0f)); };
    return rgb565(c(r), c(g), c(b));
}

// Wet paint is darker and richer; it lightens to its own colour as it dries.
Shades shadesOf(const theme::Rgb& paintRgb, float dryness) {
    float shade = theme::WET_SHADE + (1.0f - theme::WET_SHADE) * powf(dryness, 0.7f);
    Shades s;
    for (int i = 0; i < LEVELS; i++) {
        float f = shade * (1.0f + (i - MID) * LEVEL_STEP);
        s.color[i] = rgbOf(paintRgb.r * f, paintRgb.g * f, paintRgb.b * f);
        s.raw[i] = __builtin_bswap16(s.color[i]);
    }
    return s;
}

// Roller marks: a ripple per pass and a faint lap line where passes meet, different for every coat.
int rollerShade(int x, int coat, float wetness) {
    float phase = coat * 1.7f;
    float ripple = 0.6f * sinf(2 * PI_F * x / ROLLER_PX + phase) + 0.4f * sinf(2 * PI_F * x / 17.3f + phase * 2);
    float lap = (x + coat * 13) % ROLLER_PX < 2 ? -1.5f : 0.0f;
    return static_cast<int>(lroundf((ripple + lap) * ROLLER_SHADES * wetness));
}

void fillPaint(M5Canvas& c, const paint::Wall& wall, const Shades& shades) {
    float wetness = 1.0f - wall.dryness();
    float gloss = wetness * wetness;
    int roller[320];
    int sheen[SHEEN_SPAN];
    const int w = std::min(c.width(), 320), h = c.height();
    for (int x = 0; x < w; x++) roller[x] = rollerShade(x, wall.coat(), wetness);
    for (int s = 0; s < SHEEN_SPAN; s++) {
        float d = (s - SHEEN_CENTER) / SHEEN_WIDTH;
        sheen[s] = static_cast<int>(lroundf(expf(-d * d) * SHEEN_SHADES * gloss));
    }
    uint16_t* frame = static_cast<uint16_t*>(c.getBuffer());
    for (int y = 0; y < h; y++) {
        uint16_t* row = frame + y * c.width();
        for (int x = 0; x < w; x++) {
            int level = MID + roller[x] + sheen[std::min(x * 4 / 5 + y, SHEEN_SPAN - 1)];
            row[x] = shades.raw[std::min(std::max(level, 0), LEVELS - 1)];
        }
    }
}

void drawDrips(M5Canvas& c, const paint::Wall& wall, const Shades& shades) {
    for (const paint::Drip& d : wall.drips()) {
        if (d.length < 1.0f) continue;
        c.drawWideLine(d.x, 0, d.x, d.length, 2.0f, shades.color[DRIP_SHADE]);
        c.fillCircle(d.x, d.length, 3, shades.color[DRIP_SHADE]);
        c.fillCircle(d.x - 1, d.length - 1, 1, shades.color[MID + 4]);  // a glint on the bead
    }
}

// Concentric broken rings at an angle: near enough to a fingerprint at this size.
void drawPrints(M5Canvas& c, const paint::Wall& wall, const Shades& shades) {
    for (const paint::Smudge& s : wall.smudges()) {
        float start = s.turn * 360.0f / 256.0f;
        for (int ring = 1; ring <= 4; ring++) {
            float from = start + ring * 47.0f;
            c.fillArc(s.x, s.y, ring * 3 - 1, ring * 3, from, from + 290.0f, shades.color[PRINT_SHADE + ring]);
        }
    }
}

void drawSign(M5Canvas& c, bool shouting, uint32_t nowMs) {
    int x = c.width() - SIGN_W - SIGN_MARGIN, y = SIGN_MARGIN;
    if (shouting) x += static_cast<int>(lroundf(3.0f * sinf(nowMs * 0.05f)));  // a little indignant wobble
    c.fillRect(x + SIGN_W / 2 - 2, y + SIGN_H, 4, POST_H, theme::POST);
    c.fillRoundRect(x, y, SIGN_W, SIGN_H, 4, shouting ? theme::SIGN_RED : theme::SIGN);
    c.drawRoundRect(x, y, SIGN_W, SIGN_H, 4, theme::SIGN_RED);
    c.drawRoundRect(x + 1, y + 1, SIGN_W - 2, SIGN_H - 2, 3, theme::SIGN_RED);
    c.setFont(&fonts::FreeSansBold9pt7b);
    c.setTextDatum(middle_center);
    c.setTextColor(shouting ? theme::SIGN : theme::SIGN_RED);
    c.drawString(shouting ? "DON'T!" : "WET PAINT", x + SIGN_W / 2, y + SIGN_H / 2 + 1);
}

void drawCard(M5Canvas& c, const paint::Wall& wall, const Caption& caption) {
    const int x = CARD_MARGIN, y = c.height() - CARD_H - CARD_MARGIN, w = c.width() - 2 * CARD_MARGIN;
    const theme::Colour& colour = theme::coatColour(wall.coat());
    bool dry = wall.stage() == paint::Stage::Dry;
    char text[40];
    c.fillRoundRect(x, y, w, CARD_H, 8, theme::CARD);
    c.drawRoundRect(x, y, w, CARD_H, 8, theme::CARD_EDGE);

    c.setFont(&fonts::FreeSansBold9pt7b);
    c.setTextDatum(top_left);
    c.setTextColor(theme::INK);
    snprintf(text, sizeof(text), "%s, coat %d", colour.name, wall.coat());
    c.drawString(text, x + 10, y + 7);

    c.setFont(&fonts::Font2);
    c.setTextDatum(top_right);
    c.setTextColor(theme::INK_DIM);
    paint::formatRemaining(wall.remainingS(), text, sizeof(text));
    if (!dry) strncat(text, " left", sizeof(text) - strlen(text) - 1);
    c.drawString(dry ? "tap for another coat" : text, x + w - 10, y + 9);

    const int barX = x + 10, barW = w - 84, barY = y + 31;
    c.fillRoundRect(barX, barY, barW, 5, 2, theme::BAR);
    c.fillRoundRect(barX, barY, std::max(5, static_cast<int>(barW * wall.dryness())), 5, 2, theme::BAR_FILL);
    snprintf(text, sizeof(text), "%d%% dry", static_cast<int>(wall.dryness() * 100.0f));
    c.drawString(text, x + w - 10, barY - 5);

    c.setTextDatum(top_left);
    c.setTextColor(caption.reply != nullptr ? theme::SIGN_RED : theme::INK_DIM);
    const char* line = caption.reply != nullptr ? caption.reply : paint::remark(wall.stage(), wall.elapsedS());
    c.drawString(line, x + 10, y + 41);
}

}  // namespace

void drawWall(M5Canvas& canvas, const paint::Wall& wall, const Caption& caption, uint32_t nowMs) {
    Shades shades = shadesOf(theme::coatColour(wall.coat()).rgb, wall.dryness());
    fillPaint(canvas, wall, shades);
    drawDrips(canvas, wall, shades);
    drawPrints(canvas, wall, shades);
    if (wall.stage() != paint::Stage::Dry) drawSign(canvas, caption.signShouting, nowMs);
    drawCard(canvas, wall, caption);
}
