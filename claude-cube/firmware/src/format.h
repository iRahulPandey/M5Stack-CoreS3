#pragma once

#include <stddef.h>
#include <stdint.h>

// "412", "31.8K", "1.38M", "2.1B"
void formatCount(uint64_t value, char* out, size_t size);
// "42s", "38 min", "2h 05m"
void formatDuration(uint32_t seconds, char* out, size_t size);
// "$4.12", or "--" when the cost is unknown (negative); the built-in fonts are ASCII only
void formatCost(float usd, char* out, size_t size);
