#include "lines.h"

#include <stddef.h>

using drama::Reaction;

namespace {

struct LineSet {
    const char* const* lines;
    size_t count;
    uint32_t chatterMs;
};

#define LINES(name, ...) constexpr const char* name[] = {__VA_ARGS__}
LINES(IDLE, "Is anyone even looking at me?", "I'm literally the main character.", "Ugh. Mondays.",
      "I deserve a bigger desk.", "Notice me. NOW.");
LINES(BORED, "Entertain me.", "I'm SO bored.", "Hello? Anyone?", "Fine. Ignore me.");
LINES(ASLEEP, "Zzz...", "Beauty sleep. Do not disturb.", "Zzz... five more minutes...");
LINES(WOKEN, "I WAS SLEEPING!", "Rude!!", "Do you know what time it is?!");
LINES(PICKED_UP, "Careful! I'm fragile!", "Put me DOWN!", "Unhand me!", "Where are we going?!");
LINES(WHEEE, "WHEEEEEE!", "I'M FLYING!", "AAAAAAA!");
LINES(LANDED, "...nailed it.", "MY SPINE!", "I'm okay! I'm okay.", "Did you SEE that?!");
LINES(QUEASY, "I'm going to be sick...", "The room is spinning...", "Make it stop...", "BLEEEH.");
LINES(FAINTED, "*faints*", "Tell my fans... I loved them.", "Goodbye, cruel desk...");
LINES(SOBBING, "*sob*", "Nobody loves me...", "It's so dark down here...", "WAAAAH!");
LINES(BETRAYED, "How could you?!", "I'll never forget this.", "Traitor!");
LINES(RATTLED, "STOP SHAKING ME!", "Not the hair!", "I'm not a cocktail!");
LINES(DIZZY, "I see stars...", "Who am I?", "Bleh... wobbly...");
LINES(OUTRAGED, "HOW DARE YOU!", "Excuse me?!", "Did you just SLAP me?!");
LINES(FURIOUS, "I'm calling my lawyer.", "That's IT!", "You'll pay for this!", "UNBELIEVABLE!");
LINES(STARTLED, "Eek!", "Watch it!", "Was that an earthquake?!");
LINES(TICKLED, "Hehe! Stop it!", "Don't touch the face!", "Ew, fingerprints!");
LINES(SMITTEN, "Finally, some appreciation.", "More, please...", "I knew you loved me.");
#undef LINES

#define SET(name, chatterMs) {name, sizeof(name) / sizeof(name[0]), chatterMs}
constexpr LineSet SETS[] = {
    SET(IDLE, 30000),   SET(BORED, 15000),   SET(ASLEEP, 7000),    SET(WOKEN, 0),     SET(PICKED_UP, 0),
    SET(WHEEE, 0),      SET(LANDED, 0),      SET(QUEASY, 4000),    SET(FAINTED, 6000), SET(SOBBING, 4000),
    SET(BETRAYED, 0),   SET(RATTLED, 2500),  SET(DIZZY, 2500),     SET(OUTRAGED, 0),  SET(FURIOUS, 3000),
    SET(STARTLED, 0),   SET(TICKLED, 0),     SET(SMITTEN, 3000),
};
#undef SET
static_assert(sizeof(SETS) / sizeof(SETS[0]) == static_cast<size_t>(Reaction::Count), "lines for every reaction");

size_t lastPicked[static_cast<size_t>(Reaction::Count)] = {};

}  // namespace

const char* pickLine(Reaction reaction, uint32_t random) {
    size_t r = static_cast<size_t>(reaction);
    const LineSet& set = SETS[r < static_cast<size_t>(Reaction::Count) ? r : 0];
    size_t i = random % set.count;
    if (set.count > 1 && i == lastPicked[r]) i = (i + 1) % set.count;
    lastPicked[r] = i;
    return set.lines[i];
}

uint32_t chatterEveryMs(Reaction reaction) {
    size_t r = static_cast<size_t>(reaction);
    return r < static_cast<size_t>(Reaction::Count) ? SETS[r].chatterMs : 0;
}
