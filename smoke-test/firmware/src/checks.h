#pragma once

#include <stddef.h>
#include <stdint.h>

enum class Status : uint8_t { Pass, Warn, Fail, Info };

struct CheckResult {
    char name[20];
    Status status;
    char detail[44];
};

struct CheckList {
    static constexpr size_t MAX_CHECKS = 28;
    static constexpr size_t I2C_ADDRESSES = 128;

    CheckResult items[MAX_CHECKS];
    size_t count = 0;
    bool i2cFound[I2C_ADDRESSES] = {};

    void add(const char* name, Status status, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
    size_t countOf(Status status) const;
};

const char* statusName(Status status);

// Runs every hardware check in order. The speaker beeps once; Wi-Fi is switched off again afterwards.
void runChecks(CheckList& out);
