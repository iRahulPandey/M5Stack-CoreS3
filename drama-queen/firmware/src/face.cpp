#include "face.h"

#include <esp_random.h>
#include <math.h>

#include "theme.h"

using drama::Reaction;
using namespace theme;

namespace {

constexpr float PI_F = 3.14159265f;
constexpr int CX = 160;
constexpr int EYE_Y = 86;
constexpr int EYE_DX = 50;     // from the centre to each eye
constexpr int EYE_RX = 30;
constexpr int EYE_RY = 27;
constexpr int BROW_Y = 48;
constexpr int MOUTH_Y = 142;
constexpr uint32_t BLINK_EVERY_MS = 3300;
constexpr uint32_t BLINK_MS = 120;

enum class Eyes : uint8_t { Open, HalfLid, Wide, Angry, Happy, Squeezed, Sleep, Spiral, Cross, Heart };
enum class Brows : uint8_t { None, Smug, Flat, Raised, Worried, Angry };
enum class Mouth : uint8_t { Smirk, Flat, Smile, Grin, Scream, Gasp, SmallO, Wail, Wobbly, Grimace, Frown, TongueOut };

enum Extra : uint8_t { WITH_BLUSH = 1, WITH_TEARS = 2, WITH_TEAR_DROP = 4, WITH_SWEAT = 8, WITH_STARS = 16, WITH_STEAM = 32, WITH_SNORES = 64 };

struct Expression {
    uint16_t skin;
    Eyes eyes;
    Brows brows;
    Mouth mouth;
    uint8_t extras;
    float jitter;  // pixels of trembling
};

// One row per Reaction, in enum order.
constexpr Expression EXPRESSIONS[] = {
    {SKIN, Eyes::Open, Brows::Smug, Mouth::Smirk, 0, 0},                     // Idle
    {SKIN, Eyes::HalfLid, Brows::Flat, Mouth::Flat, 0, 0},                   // Bored
    {NIGHT, Eyes::Sleep, Brows::None, Mouth::SmallO, WITH_SNORES, 0},             // Asleep
    {SKIN_CROSS, Eyes::HalfLid, Brows::Angry, Mouth::Grimace, WITH_STEAM, 1.5f},  // Woken
    {SKIN, Eyes::Wide, Brows::Worried, Mouth::SmallO, WITH_SWEAT, 1.0f},          // PickedUp
    {SKIN_SCREAM, Eyes::Wide, Brows::Raised, Mouth::Scream, 0, 3.0f},        // Wheee
    {SKIN, Eyes::HalfLid, Brows::Smug, Mouth::Smirk, WITH_SWEAT, 0},              // Landed
    {SKIN_SICK, Eyes::HalfLid, Brows::Worried, Mouth::Wobbly, WITH_SWEAT, 0},     // Queasy
    {SKIN_FAINT, Eyes::Cross, Brows::None, Mouth::TongueOut, 0, 0},          // Fainted
    {SKIN_SAD, Eyes::Squeezed, Brows::Worried, Mouth::Wail, WITH_TEARS, 1.0f},    // Sobbing
    {SKIN, Eyes::Open, Brows::Angry, Mouth::Frown, WITH_TEAR_DROP, 0},            // Betrayed
    {SKIN, Eyes::Wide, Brows::Worried, Mouth::Grimace, WITH_SWEAT, 4.0f},         // Rattled
    {SKIN_DIZZY, Eyes::Spiral, Brows::None, Mouth::Wobbly, WITH_STARS, 0},        // Dizzy
    {SKIN_CROSS, Eyes::Angry, Brows::Angry, Mouth::Gasp, 0, 2.0f},           // Outraged
    {SKIN_FURIOUS, Eyes::Angry, Brows::Angry, Mouth::Grimace, WITH_STEAM, 3.0f},  // Furious
    {SKIN, Eyes::Wide, Brows::Raised, Mouth::SmallO, WITH_SWEAT, 0},              // Startled
    {SKIN, Eyes::Happy, Brows::Raised, Mouth::Grin, WITH_BLUSH, 0},               // Tickled
    {SKIN, Eyes::Heart, Brows::Raised, Mouth::Smile, WITH_BLUSH, 0},              // Smitten
};
static_assert(sizeof(EXPRESSIONS) / sizeof(EXPRESSIONS[0]) == static_cast<size_t>(Reaction::Count),
              "an expression for every reaction");

struct Frame {  // what every part needs: offsets for this frame and the colours to draw with
    float ox, oy;
    uint16_t skin;
    uint32_t nowMs;
    float pupilX, pupilY;
};

float jitterOf(float amount) {
    return amount > 0 ? (static_cast<float>(esp_random() % 2001) / 1000.0f - 1.0f) * amount : 0.0f;
}

float wave(uint32_t nowMs, uint32_t periodMs) {
    return sinf(2.0f * PI_F * static_cast<float>(nowMs % periodMs) / periodMs);
}

int eyeX(int side, const Frame& f) {
    return lroundf(CX + side * EYE_DX + f.ox);
}

void drawCrown(M5Canvas& c, const Frame& f) {
    int x = lroundf(CX + f.ox), y = lroundf(f.oy);
    c.fillRect(x - 28, y + 20, 56, 9, CROWN);
    c.fillTriangle(x - 28, y + 21, x - 12, y + 21, x - 22, y + 4, CROWN);
    c.fillTriangle(x - 10, y + 21, x + 10, y + 21, x, y + 1, CROWN);
    c.fillTriangle(x + 12, y + 21, x + 28, y + 21, x + 22, y + 4, CROWN);
    c.fillCircle(x - 22, y + 5, 3, GEM);
    c.fillCircle(x, y + 2, 3, GEM);
    c.fillCircle(x + 22, y + 5, 3, GEM);
    c.fillCircle(x, y + 24, 3, GEM);
}

void drawLashes(M5Canvas& c, int side, int ex, int ey) {
    for (float deg : {205.0f, 228.0f, 251.0f}) {  // on the outer top edge
        float a = (side < 0 ? deg : 540.0f - deg) * PI_F / 180.0f;
        float x0 = ex + (EYE_RX + 2) * cosf(a), y0 = ey + (EYE_RY + 2) * sinf(a);
        c.drawWideLine(x0, y0, x0 + 9 * cosf(a), y0 + 9 * sinf(a), 2.0f, INK);
    }
}

void drawOpenEye(M5Canvas& c, int side, Eyes kind, const Frame& f) {
    int ex = eyeX(side, f), ey = lroundf(EYE_Y + f.oy);
    bool wide = kind == Eyes::Wide;
    int rx = wide ? EYE_RX + 4 : EYE_RX, ry = wide ? EYE_RY + 6 : EYE_RY;
    c.fillEllipse(ex, ey, rx + 3, ry + 3, INK);
    c.fillEllipse(ex, ey, rx, ry, PAPER);
    int pr = wide ? 8 : 12;
    int px = ex + lroundf(f.pupilX), py = ey + lroundf(f.pupilY);
    c.fillCircle(px, py, pr, INK);
    c.fillCircle(px + pr / 3, py - pr / 3, pr / 3 + 1, PAPER);
    if (kind == Eyes::HalfLid) {
        int lid = ey - ry / 6;
        c.fillRect(ex - rx - 5, ey - ry - 5, 2 * rx + 10, lid - (ey - ry - 5), f.skin);
        c.drawWideLine(ex - rx - 2, lid, ex + rx + 2, lid, 3.0f, INK);
    } else if (kind == Eyes::Angry) {  // the inner top corner is cut away by a scowl
        int inner = ex - side * (rx + 5), outer = ex + side * (rx + 5), cut = ey + ry / 8;
        c.fillTriangle(outer, ey - ry - 5, inner, ey - ry - 5, inner, cut, f.skin);
        c.drawWideLine(outer, ey - ry + 2, inner, cut, 3.0f, INK);
    }
    if (kind != Eyes::Angry) drawLashes(c, side, ex, ey);
}

void drawSpiralEye(M5Canvas& c, int ex, int ey, float phase) {
    float px = ex, py = ey;
    for (float t = 0.3f; t < 4.0f * PI_F; t += 0.3f) {
        float r = 2.0f + t * 2.2f, a = t + phase;
        float x = ex + cosf(a) * r, y = ey + sinf(a) * r;
        c.drawWideLine(px, py, x, y, 2.5f, INK);
        px = x;
        py = y;
    }
}

void drawHeart(M5Canvas& c, float x, float y, float s, uint16_t color) {
    c.fillCircle(lroundf(x - s * 0.5f), lroundf(y - s * 0.2f), lroundf(s * 0.55f), color);
    c.fillCircle(lroundf(x + s * 0.5f), lroundf(y - s * 0.2f), lroundf(s * 0.55f), color);
    c.fillTriangle(lroundf(x - s * 1.04f), lroundf(y), lroundf(x + s * 1.04f), lroundf(y), lroundf(x),
                   lroundf(y + s * 1.15f), color);
}

void drawClosedEye(M5Canvas& c, int side, Eyes kind, const Frame& f) {
    int ex = eyeX(side, f), ey = lroundf(EYE_Y + f.oy);
    switch (kind) {
        case Eyes::Happy: c.fillArc(ex, ey + 10, EYE_RY - 7, EYE_RY, 200, 340, INK); break;
        case Eyes::Sleep: c.fillArc(ex, ey - 12, EYE_RY - 7, EYE_RY, 20, 160, INK); break;
        case Eyes::Squeezed:  // > <
            c.drawWideLine(ex - side * 16, ey - 12, ex + side * 14, ey, 3.5f, INK);
            c.drawWideLine(ex + side * 14, ey, ex - side * 16, ey + 12, 3.5f, INK);
            break;
        case Eyes::Cross:
            c.drawWideLine(ex - 15, ey - 15, ex + 15, ey + 15, 4.0f, INK);
            c.drawWideLine(ex - 15, ey + 15, ex + 15, ey - 15, 4.0f, INK);
            break;
        case Eyes::Spiral: drawSpiralEye(c, ex, ey, f.nowMs * 0.01f * side); break;
        case Eyes::Heart: drawHeart(c, ex, ey - 4, 22.0f * (1.0f + 0.1f * wave(f.nowMs, 500)), HEART); break;
        default: break;
    }
}

void drawEyes(M5Canvas& c, Eyes kind, const Frame& f) {
    bool open = kind == Eyes::Open || kind == Eyes::HalfLid || kind == Eyes::Wide || kind == Eyes::Angry;
    bool blinking = open && kind != Eyes::Angry && f.nowMs % BLINK_EVERY_MS < BLINK_MS;
    for (int side : {-1, 1}) {
        if (blinking) {
            c.fillArc(eyeX(side, f), lroundf(EYE_Y + f.oy - 12), EYE_RY - 7, EYE_RY, 20, 160, INK);
        } else if (open) {
            drawOpenEye(c, side, kind, f);
        } else {
            drawClosedEye(c, side, kind, f);
        }
    }
}

void drawBrows(M5Canvas& c, Brows kind, const Frame& f) {
    if (kind == Brows::None) return;
    for (int side : {-1, 1}) {
        int inner = 0, outer = 0;  // vertical offsets of each end
        switch (kind) {
            case Brows::Smug: inner = side < 0 ? -2 : -14; outer = side < 0 ? 0 : -10; break;
            case Brows::Raised: inner = -12; outer = -10; break;
            case Brows::Worried: inner = -12; outer = 2; break;
            case Brows::Angry: inner = 10; outer = -6; break;
            default: break;
        }
        int ex = eyeX(side, f), y = lroundf(BROW_Y + f.oy);
        c.drawWideLine(ex - side * 22, y + inner, ex + side * 26, y + outer, 4.5f, INK);
    }
}

void drawMouthShape(M5Canvas& c, Mouth kind, int x, int y, uint32_t nowMs) {
    switch (kind) {
        case Mouth::Smirk: c.fillArc(x + 8, y - 16, 20, 26, 30, 115, INK); break;
        case Mouth::Smile: c.fillArc(x, y - 18, 22, 28, 25, 155, INK); break;
        case Mouth::Frown: c.fillArc(x, y + 24, 22, 28, 205, 335, INK); break;
        case Mouth::Flat: c.fillRoundRect(x - 22, y - 2, 44, 6, 3, INK); break;
        case Mouth::SmallO: c.fillEllipse(x, y, 7, 8, INK); break;
        case Mouth::Gasp:
            c.fillEllipse(x, y, 13, 16, INK);
            c.fillEllipse(x, y, 9, 12, MOUTH);
            break;
        case Mouth::Scream:
        case Mouth::Wail: {
            bool scream = kind == Mouth::Scream;
            int rx = scream ? 20 : 28, ry = scream ? 26 : 17;
            c.fillEllipse(x, y + 2, rx, ry, INK);
            c.fillEllipse(x, y + 2, rx - 4, ry - 4, MOUTH);
            c.fillEllipse(x, y + ry - 6, rx / 2, 6, TONGUE);
            break;
        }
        case Mouth::Grin:
            c.fillArc(x, y - 6, 0, 30, 0, 180, MOUTH);
            c.fillEllipse(x, y + 12, 14, 7, TONGUE);
            c.drawWideLine(x - 30, y - 6, x + 30, y - 6, 2.5f, INK);
            break;
        case Mouth::Grimace:
            c.fillRoundRect(x - 34, y - 12, 68, 26, 8, INK);
            c.fillRoundRect(x - 31, y - 9, 62, 20, 6, PAPER);
            for (int t : {-20, -7, 7, 20}) c.drawFastVLine(x + t, y - 9, 20, INK);
            c.drawFastHLine(x - 31, y + 1, 62, INK);
            break;
        case Mouth::Wobbly: {
            float phase = nowMs * 0.012f;
            for (int i = 0; i < 4; i++) {
                float x0 = x - 26 + i * 13, x1 = x0 + 13;
                c.drawWideLine(x0, y + 5 * sinf(phase + i * 1.6f), x1, y + 5 * sinf(phase + (i + 1) * 1.6f), 3.0f, INK);
            }
            break;
        }
        case Mouth::TongueOut:
            c.fillRoundRect(x - 24, y - 2, 48, 6, 3, INK);
            c.fillRoundRect(x + 2, y + 2, 16, 18, 8, TONGUE);
            break;
    }
}

void drawTears(M5Canvas& c, const Frame& f) {
    for (int side : {-1, 1}) {
        for (int i = 0; i < 3; i++) {  // drops sliding down each cheek, staggered
            int x = eyeX(side, f) + side * 22;
            int y = lroundf(EYE_Y + f.oy + 24 + (f.nowMs / 6 + i * 30) % 90);
            c.fillCircle(x, y, 5, TEAR);
            c.fillTriangle(x - 5, y - 1, x + 5, y - 1, x, y - 11, TEAR);
        }
    }
}

void drawExtras(M5Canvas& c, uint8_t extras, const Frame& f) {
    int oy = lroundf(f.oy);
    if (extras & WITH_BLUSH) {
        for (int side : {-1, 1}) c.fillEllipse(eyeX(side, f) + side * 8, EYE_Y + oy + 40, 18, 8, BLUSH);
    }
    if (extras & WITH_TEARS) drawTears(c, f);
    if (extras & WITH_TEAR_DROP) {
        int x = eyeX(1, f) + 30, y = EYE_Y + oy + 22;
        c.fillCircle(x, y, 5, TEAR);
        c.fillTriangle(x - 5, y - 1, x + 5, y - 1, x, y - 11, TEAR);
    }
    if (extras & WITH_SWEAT) {
        int x = 268, y = 40 + lroundf(3 * wave(f.nowMs, 900)) + oy;
        c.fillCircle(x, y, 7, TEAR);
        c.fillTriangle(x - 7, y - 2, x + 7, y - 2, x, y - 16, TEAR);
    }
    if (extras & WITH_STARS) {
        for (int i = 0; i < 3; i++) {  // circling the crown
            float a = f.nowMs * 0.004f + i * 2.094f;
            int x = lroundf(CX + 70 * cosf(a)), y = lroundf(22 + 12 * sinf(a));
            c.fillEllipse(x, y, 8, 2, STAR);
            c.fillEllipse(x, y, 2, 8, STAR);
        }
    }
    if (extras & WITH_STEAM) {
        for (int side : {-1, 1}) {
            float rise = static_cast<float>(f.nowMs % 900) / 900.0f;
            int x = CX + side * 120, y = lroundf(46 - rise * 30);
            c.fillCircle(x, y, lroundf(8 + rise * 6), STEAM);
            c.fillCircle(x + side * 10, y + 10, 7, STEAM);
        }
    }
    if (extras & WITH_SNORES) {
        c.setFont(&fonts::FreeSansBold12pt7b);
        c.setTextColor(SNORE);
        c.setTextDatum(middle_center);
        for (int i = 0; i < 3; i++) {
            float p = fmodf(f.nowMs / 1000.0f + i * 0.9f, 2.7f) / 2.7f;
            c.setTextSize(0.8f + p);
            c.drawString("z", lroundf(250 + p * 30), lroundf(110 - p * 80));
        }
        c.setTextSize(1);
    }
}

// Where the pupils point: following a tilt, rolling when queasy, glancing about otherwise.
void aimPupils(Frame& f, Reaction r, const drama::MotionState& m) {
    if (r == Reaction::Queasy) {
        float a = f.nowMs * 0.006f;
        f.pupilX = 9 * cosf(a);
        f.pupilY = 7 * sinf(a);
    } else if (r == Reaction::Idle || r == Reaction::Bored) {
        int slot = (f.nowMs / 2500) % 4;  // a slow, slightly bored look around
        const float xs[] = {0, 10, 0, -10};
        f.pupilX = xs[slot] + m.leanX * 20;
        f.pupilY = (r == Reaction::Bored ? 6.0f : 0.0f) + m.leanY * 14;
    } else if (r == Reaction::Wheee || r == Reaction::Rattled) {
        f.pupilX = jitterOf(6);
        f.pupilY = jitterOf(6);
    }
}

}  // namespace

void drawFace(M5Canvas& c, Reaction reaction, uint32_t nowMs, uint32_t sinceMs, const drama::MotionState& motion) {
    (void)sinceMs;
    size_t i = static_cast<size_t>(reaction);
    const Expression& e = EXPRESSIONS[i < static_cast<size_t>(Reaction::Count) ? i : 0];
    Frame f{jitterOf(e.jitter), jitterOf(e.jitter), e.skin, nowMs, 0, 0};
    if (reaction == Reaction::Wheee) f.oy += 6 * wave(nowMs, 400);
    if (reaction == Reaction::Smitten) f.ox += 4 * wave(nowMs, 1600);
    aimPupils(f, reaction, motion);

    c.fillScreen(e.skin);
    drawCrown(c, f);
    drawBrows(c, e.brows, f);
    drawEyes(c, e.eyes, f);
    drawMouthShape(c, e.mouth, lroundf(CX + f.ox), lroundf(MOUTH_Y + f.oy), nowMs);
    drawExtras(c, e.extras, f);
}
