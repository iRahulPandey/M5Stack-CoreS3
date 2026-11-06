#pragma once

// Memory bandwidth in MB/s, where 1 MB = 10^6 bytes (the unit the season plan uses).
struct MemBenchResults {
    bool psramOk;
    float sramRead;
    float psramRead;
    float psramReadDualCore;
    float psramWrite;
    float flashRead;
};

MemBenchResults runMemBench();
