#pragma once

#include <stdint.h>

#include "checks.h"
#include "memBench.h"

// Upper bounds on llama2.c-style decoding speed: every token reads every weight once,
// so tokens/s cannot beat (PSRAM read bandwidth) / (model bytes). Sizes match the E01 plan.
struct E01Ceiling {
    float bandwidthMBps;     // best of single-core and both-core PSRAM reads
    float int8_3m3TokS;      // 3.3M parameters, INT8: 3.3 MB of weights
    float fp32_260kTokS;     // 260K parameters, FP32: 1.04 MB of weights
};

E01Ceiling e01Ceiling(const MemBenchResults& bench);

// One line, "REPORT {json}", that smoke-test/host/smoke.py reads back.
void printReport(const CheckList& checks, const MemBenchResults& bench, uint32_t elapsedMs);
