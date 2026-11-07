#include "words.h"

#include <stdio.h>

namespace paint {

namespace {

constexpr float REMARK_EVERY_S = 5 * 60;

const char* const WET[] = {
    "Still wet.",
    "Drying has begun. Probably.",
    "It is wet, as expected.",
    "Nothing to report.",
    "The paint is doing its best.",
};
const char* const TACKY[] = {
    "Slightly less wet.",
    "Tacky. Still not interesting.",
    "Progress is being made, technically.",
    "Please do not touch. Or watch.",
};
const char* const TOUCH_DRY[] = {
    "Touch-dry. Another coat recommended.",
    "Almost dry. Relatively.",
    "Still drying, just more quietly.",
    "It looks dry. It is not dry.",
};
const char* const DRY[] = {
    "Dry. Thank you for watching.",
};

template <size_t N>
const char* pick(const char* const (&lines)[N], float elapsedS) {
    return lines[static_cast<size_t>(elapsedS / REMARK_EVERY_S) % N];
}

}  // namespace

const char* stageName(Stage stage) {
    switch (stage) {
        case Stage::Wet: return "wet";
        case Stage::Tacky: return "tacky";
        case Stage::TouchDry: return "touch-dry";
        case Stage::Dry: return "dry";
    }
    return "wet";
}

void formatRemaining(float seconds, char* out, size_t size) {
    if (seconds <= 0.0f) {
        snprintf(out, size, "dry");
        return;
    }
    int minutes = static_cast<int>(seconds / 60.0f);
    if (minutes < 1) {
        snprintf(out, size, "under a minute");
    } else if (minutes < 60) {
        snprintf(out, size, "%d min", minutes);
    } else {
        snprintf(out, size, "%d h %d min", minutes / 60, minutes % 60);
    }
}

const char* remark(Stage stage, float elapsedS) {
    switch (stage) {
        case Stage::Wet: return pick(WET, elapsedS);
        case Stage::Tacky: return pick(TACKY, elapsedS);
        case Stage::TouchDry: return pick(TOUCH_DRY, elapsedS);
        case Stage::Dry: break;
    }
    return pick(DRY, elapsedS);
}

const char* touchReply(Touched touched) {
    switch (touched) {
        case Touched::Smudged: return "You touched it. That's there now.";
        case Touched::Spared: return "Touch-dry. You got away with it.";
        case Touched::Recoated: return "Another coat. Of course.";
    }
    return "";
}

}  // namespace paint
