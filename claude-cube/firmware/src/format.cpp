#include "format.h"

#include <stdio.h>

namespace {

struct Scale {
    double divisor;
    const char* suffix;
};

constexpr Scale SCALES[] = {{1e9, "B"}, {1e6, "M"}, {1e3, "K"}};

}  // namespace

void formatCount(uint64_t value, char* out, size_t size) {
    for (const Scale& s : SCALES) {
        if (value >= s.divisor * 0.9995) {  // 999,950 reads "1.00M", not "1000K"
            double v = static_cast<double>(value) / s.divisor;
            // three significant digits: 1.38M, 31.8K, 412K
            const char* fmt = v >= 100 ? "%.0f%s" : (v >= 10 ? "%.1f%s" : "%.2f%s");
            snprintf(out, size, fmt, v, s.suffix);
            return;
        }
    }
    snprintf(out, size, "%llu", static_cast<unsigned long long>(value));
}

void formatDuration(uint32_t seconds, char* out, size_t size) {
    if (seconds < 60) {
        snprintf(out, size, "%lus", static_cast<unsigned long>(seconds));
    } else if (seconds < 3600) {
        snprintf(out, size, "%lu min", static_cast<unsigned long>(seconds / 60));
    } else {
        snprintf(out, size, "%luh %02lum", static_cast<unsigned long>(seconds / 3600),
                 static_cast<unsigned long>((seconds % 3600) / 60));
    }
}

void formatCost(float usd, char* out, size_t size) {
    if (usd < 0) {
        snprintf(out, size, "--");
    } else if (usd < 100) {
        snprintf(out, size, "$%.2f", usd);
    } else {
        snprintf(out, size, "$%.0f", usd);
    }
}
