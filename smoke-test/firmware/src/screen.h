#pragma once

#include <M5Unified.h>

#include "checks.h"
#include "memBench.h"

enum class Screen : uint8_t { ChecksFirst, ChecksSecond, Memory, Count };

Screen nextScreen(Screen screen);

void drawRunning(M5Canvas& canvas);
void drawScreen(M5Canvas& canvas, Screen screen, const CheckList& checks, const MemBenchResults& bench);
