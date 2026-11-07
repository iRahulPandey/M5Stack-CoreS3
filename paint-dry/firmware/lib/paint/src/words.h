#pragma once

#include <stddef.h>

#include "wall.h"

namespace paint {

const char* stageName(Stage stage);  // "wet", "tacky", "touch-dry", "dry"

// "2 h 38 min", "45 min", "under a minute", or "dry" once nothing is left.
void formatRemaining(float seconds, char* out, size_t size);

// A dull remark about the paint, which changes every few minutes.
const char* remark(Stage stage, float elapsedS);

// What the wall says back when touched.
const char* touchReply(Touched touched);

}  // namespace paint
