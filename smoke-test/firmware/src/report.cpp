#include "report.h"

#include <Arduino.h>
#include <ArduinoJson.h>

namespace {

constexpr int REPORT_VERSION = 1;
constexpr float INT8_3M3_MB = 3.3f;
constexpr float FP32_260K_MB = 1.04f;

float rounded(float v) {
    return roundf(v * 10.0f) / 10.0f;
}

}  // namespace

E01Ceiling e01Ceiling(const MemBenchResults& bench) {
    float bw = max(bench.psramRead, bench.psramReadDualCore);
    return {bw, bw / INT8_3M3_MB, bw / FP32_260K_MB};
}

void printReport(const CheckList& checks, const MemBenchResults& bench, uint32_t elapsedMs) {
    JsonDocument doc;
    doc["v"] = REPORT_VERSION;
    doc["ms"] = elapsedMs;
    JsonArray list = doc["checks"].to<JsonArray>();
    for (size_t i = 0; i < checks.count; i++) {
        JsonObject c = list.add<JsonObject>();
        c["name"] = checks.items[i].name;
        c["status"] = statusName(checks.items[i].status);
        c["detail"] = checks.items[i].detail;
    }
    JsonArray i2c = doc["i2c"].to<JsonArray>();
    for (size_t a = 0; a < CheckList::I2C_ADDRESSES; a++) {
        if (checks.i2cFound[a]) i2c.add(a);
    }
    JsonObject b = doc["bench"].to<JsonObject>();
    b["sram_read"] = rounded(bench.sramRead);
    b["psram_read"] = rounded(bench.psramRead);
    b["psram_read_2core"] = rounded(bench.psramReadDualCore);
    b["psram_write"] = rounded(bench.psramWrite);
    b["flash_read"] = rounded(bench.flashRead);
    E01Ceiling e = e01Ceiling(bench);
    JsonObject e01 = doc["e01"].to<JsonObject>();
    e01["bandwidth"] = rounded(e.bandwidthMBps);
    e01["int8_3m3_tok_s"] = rounded(e.int8_3m3TokS);
    e01["fp32_260k_tok_s"] = rounded(e.fp32_260kTokS);

    Serial.print("REPORT ");
    serializeJson(doc, Serial);
    Serial.print('\n');
}
