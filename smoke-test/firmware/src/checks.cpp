#include "checks.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t I2C_FREQ = 400000;
constexpr uint32_t EXPECTED_FLASH_BYTES = 16u * 1024 * 1024;
constexpr uint32_t MIN_PSRAM_BYTES = 8000000;  // 8 MB part, minus what the allocator keeps
constexpr int IMU_SAMPLES = 25;
constexpr float GRAVITY_TOLERANCE_G = 0.15f;
constexpr float REST_GYRO_DPS = 5.0f;
constexpr int MAG_UPDATES = 30;
constexpr uint32_t MIC_SAMPLE_RATE = 16000;
constexpr size_t MIC_SAMPLES = 8000;  // half a second
constexpr int16_t MIC_CLIP = 32767;
constexpr int NO_BATTERY_MV = 2500;  // a 1S Li-ion never reads this low; the pin is floating
constexpr int MIN_BATTERY_MV = 3000;
constexpr int MAX_BATTERY_MV = 4400;
constexpr uint16_t MIN_RTC_YEAR = 2024;

struct I2cDevice {
    const char* name;
    uint8_t address;
    int16_t idRegister;  // -1: presence only
    uint8_t idValue;
    bool optional;       // a missing ACK is a warning, not a failure
};

// Addresses from M5Stack's CoreS3 docs; ID registers and values from each part's datasheet.
constexpr I2cDevice DEVICES[] = {
    {"PMIC AXP2101", 0x34, -1, 0, false},
    {"Amp AW88298", 0x36, -1, 0, false},
    {"Touch FT6336U", 0x38, 0xA3, 0x64, false},
    {"Codec ES7210", 0x40, -1, 0, false},
    {"RTC BM8563", 0x51, -1, 0, false},
    {"IO exp AW9523B", 0x58, 0x10, 0x23, false},
    {"IMU BMI270", 0x69, 0x00, 0x24, false},
    {"Light LTR-553", 0x23, 0x86, 0x92, false},
    {"Camera GC0308", 0x21, 0x00, 0x9B, true},  // may not ACK until its clock and reset are set up
};

bool isKnownAddress(uint8_t address) {
    for (const I2cDevice& d : DEVICES) {
        if (d.address == address) return true;
    }
    return false;
}

void checkChip(CheckList& out) {
    uint32_t flash = ESP.getFlashChipSize();
    bool ok = strstr(ESP.getChipModel(), "ESP32-S3") != nullptr && flash == EXPECTED_FLASH_BYTES;
    // No revision here: Arduino's getChipRevision() reports only the major part (0 for a v0.2 chip).
    out.add("Chip", ok ? Status::Pass : Status::Fail, "%s, %d cores, %lu MHz, %lu MB flash", ESP.getChipModel(),
            ESP.getChipCores(), static_cast<unsigned long>(ESP.getCpuFreqMHz()), static_cast<unsigned long>(flash >> 20));
}

void checkBoardAndPsram(CheckList& out) {
    bool cores3 = M5.getBoard() == m5::board_t::board_M5StackCoreS3;
    out.add("Board", cores3 ? Status::Pass : Status::Warn, cores3 ? "M5Stack CoreS3" : "detected board id %d",
            static_cast<int>(M5.getBoard()));
    uint32_t psram = ESP.getPsramSize();
    out.add("PSRAM", psram >= MIN_PSRAM_BYTES ? Status::Pass : Status::Fail, "%lu KB total, %lu KB free",
            static_cast<unsigned long>(psram >> 10), static_cast<unsigned long>(ESP.getFreePsram() >> 10));
}

void checkI2cDevice(CheckList& out, const I2cDevice& d) {
    if (!out.i2cFound[d.address]) {
        out.add(d.name, d.optional ? Status::Warn : Status::Fail, "no ACK at 0x%02X", d.address);
        return;
    }
    if (d.idRegister < 0) {
        out.add(d.name, Status::Pass, "ACK at 0x%02X", d.address);
        return;
    }
    uint8_t id = M5.In_I2C.readRegister8(d.address, static_cast<uint8_t>(d.idRegister), I2C_FREQ);
    out.add(d.name, id == d.idValue ? Status::Pass : Status::Fail, "0x%02X, id 0x%02X (expect 0x%02X)", d.address,
            id, d.idValue);
}

void checkI2c(CheckList& out) {
    M5.In_I2C.scanID(out.i2cFound);
    for (const I2cDevice& d : DEVICES) {
        checkI2cDevice(out, d);
    }
    char extra[44] = "";
    size_t len = 0;
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (out.i2cFound[a] && !isKnownAddress(a) && len + 6 < sizeof(extra)) {
            len += snprintf(extra + len, sizeof(extra) - len, "0x%02X ", a);
        }
    }
    out.add("Other I2C", Status::Info, "%s", len ? extra : "none");
}

void checkImu(CheckList& out) {
    if (!M5.Imu.isEnabled()) {
        out.add("IMU motion", Status::Fail, "IMU driver did not start");
        return;
    }
    float accel = 0, gyro = 0;
    for (int i = 0; i < IMU_SAMPLES; i++) {
        float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
        M5.Imu.update();
        M5.Imu.getAccel(&ax, &ay, &az);
        M5.Imu.getGyro(&gx, &gy, &gz);
        accel += sqrtf(ax * ax + ay * ay + az * az);
        gyro += sqrtf(gx * gx + gy * gy + gz * gz);
        delay(10);
    }
    accel /= IMU_SAMPLES;
    gyro /= IMU_SAMPLES;
    bool atRest = fabsf(accel - 1.0f) < GRAVITY_TOLERANCE_G && gyro < REST_GYRO_DPS;
    out.add("IMU motion", atRest ? Status::Pass : Status::Warn, "%.2f g, %.1f dps%s", accel, gyro,
            atRest ? "" : " (was it moving?)");
}

void checkMagnetometer(CheckList& out) {
    float mx = 0, my = 0, mz = 0;
    for (int i = 0; i < MAG_UPDATES; i++) {
        M5.Imu.update();
        M5.Imu.getMag(&mx, &my, &mz);
        delay(10);
    }
    float field = sqrtf(mx * mx + my * my + mz * mz);
    out.add("Compass BMM150", field > 0.0f ? Status::Pass : Status::Fail, "|B| %.1f (x %.1f y %.1f z %.1f)", field,
            mx, my, mz);
}

void checkRtc(CheckList& out) {
    if (!M5.Rtc.isEnabled()) {
        out.add("RTC", Status::Fail, "RTC driver did not start");
        return;
    }
    m5::rtc_datetime_t dt;
    bool valid = M5.Rtc.getDateTime(&dt) && dt.date.year >= MIN_RTC_YEAR && dt.date.month >= 1 && dt.date.date >= 1 &&
                 dt.time.hours >= 0 && dt.time.minutes >= 0;
    if (!valid) {
        out.add("RTC", Status::Warn, "no valid time (clock lost power?)");
        return;
    }
    out.add("RTC", Status::Pass, "%04d-%02d-%02d %02d:%02d", dt.date.year, dt.date.month, dt.date.date, dt.time.hours,
            dt.time.minutes);
}

void checkPower(CheckList& out) {
    bool axp = M5.Power.getType() == m5::Power_Class::pmic_t::pmic_axp2101;
    int mv = M5.Power.getBatteryVoltage();
    int level = M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging() == m5::Power_Class::is_charging_t::is_charging;
    int vbus = M5.Power.getVBUSVoltage();
    if (mv < NO_BATTERY_MV) {
        out.add("Battery", Status::Warn, "none? reads %d mV, USB %d mV", mv, vbus);
        return;
    }
    bool sane = axp && mv > MIN_BATTERY_MV && mv < MAX_BATTERY_MV;
    out.add("Battery", sane ? Status::Pass : Status::Warn, "%d mV, %d%%, %s, USB %d mV", mv, level,
            charging ? "charging" : "not charging", vbus);
}

void checkSpeaker(CheckList& out) {
    M5.Speaker.setVolume(64);
    bool started = M5.Speaker.tone(880, 150);
    delay(20);
    bool playing = M5.Speaker.isPlaying();
    for (int i = 0; i < 50 && M5.Speaker.isPlaying(); i++) {
        delay(10);
    }
    out.add("Speaker", started && playing ? Status::Pass : Status::Fail, "880 Hz beep %s",
            started && playing ? "played (listen)" : "did not start");
}

void summarizeMic(CheckList& out, const int16_t* samples, size_t n) {
    int64_t sum = 0;
    int peak = 0;
    for (size_t i = 0; i < n; i++) sum += samples[i];
    int mean = static_cast<int>(sum / static_cast<int64_t>(n));
    double energy = 0;
    for (size_t i = 0; i < n; i++) {
        int v = samples[i] - mean;
        energy += static_cast<double>(v) * v;
        peak = max(peak, abs(v));
    }
    int rms = static_cast<int>(sqrt(energy / n));
    Status s = peak == 0 ? Status::Fail : (peak >= MIC_CLIP ? Status::Warn : Status::Pass);
    out.add("Microphones", s, "rms %d, peak %d%s", rms, peak, peak == 0 ? " (silent)" : "");
}

void checkMic(CheckList& out) {
    auto* samples = static_cast<int16_t*>(heap_caps_malloc(MIC_SAMPLES * sizeof(int16_t), MALLOC_CAP_8BIT));
    if (samples == nullptr) {
        out.add("Microphones", Status::Fail, "no memory for the recording");
        return;
    }
    M5.Speaker.end();  // mic and speaker share the I2S bus
    bool ok = M5.Mic.begin() && M5.Mic.record(samples, MIC_SAMPLES, MIC_SAMPLE_RATE);
    while (ok && M5.Mic.isRecording()) delay(5);
    M5.Mic.end();
    M5.Speaker.begin();
    if (ok) {
        summarizeMic(out, samples, MIC_SAMPLES);
    } else {
        out.add("Microphones", Status::Fail, "recording did not start");
    }
    heap_caps_free(samples);
}

// Counts networks and the strongest signal only; network names never leave the radio.
void checkWifi(CheckList& out) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    int n = WiFi.scanNetworks(false, true);
    int best = -127;
    for (int i = 0; i < n; i++) best = max(best, static_cast<int>(WiFi.RSSI(i)));
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
    if (n < 0) {
        out.add("Wi-Fi radio", Status::Fail, "scan failed (%d)", n);
    } else {
        out.add("Wi-Fi radio", n > 0 ? Status::Pass : Status::Warn, "%d networks, best %d dBm", n, n ? best : 0);
    }
}

}  // namespace

void CheckList::add(const char* name, Status status, const char* fmt, ...) {
    if (count >= MAX_CHECKS) return;
    CheckResult& r = items[count++];
    strlcpy(r.name, name, sizeof(r.name));
    r.status = status;
    va_list args;
    va_start(args, fmt);
    vsnprintf(r.detail, sizeof(r.detail), fmt, args);
    va_end(args);
}

size_t CheckList::countOf(Status status) const {
    size_t n = 0;
    for (size_t i = 0; i < count; i++) {
        if (items[i].status == status) n++;
    }
    return n;
}

const char* statusName(Status status) {
    switch (status) {
        case Status::Pass: return "pass";
        case Status::Warn: return "warn";
        case Status::Fail: return "fail";
        case Status::Info: return "info";
    }
    return "info";
}

void runChecks(CheckList& out) {
    out = CheckList();
    checkChip(out);
    checkBoardAndPsram(out);
    checkI2c(out);
    checkImu(out);
    checkMagnetometer(out);
    checkRtc(out);
    checkPower(out);
    checkSpeaker(out);
    checkMic(out);
    checkWifi(out);
}
