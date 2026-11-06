#pragma once

#include <stdint.h>

#include "drama.h"

// What she says for each reaction: a random line, never the same one twice in a row.
const char* pickLine(drama::Reaction reaction, uint32_t random);

// How often she pipes up again while a reaction lasts (0: only once, when it starts).
uint32_t chatterEveryMs(drama::Reaction reaction);
