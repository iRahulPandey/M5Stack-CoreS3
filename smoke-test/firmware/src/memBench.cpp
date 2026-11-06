#include "memBench.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_spi_flash.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <string.h>

namespace {

constexpr size_t PSRAM_BUF_BYTES = 4 * 1024 * 1024;  // far larger than the data cache
constexpr size_t SRAM_BUF_BYTES = 64 * 1024;
constexpr size_t FLASH_MAP_BYTES = 1024 * 1024;
constexpr int SRAM_PASSES = 64;
constexpr int PSRAM_PASSES = 4;
constexpr int READER_PRIORITY = 5;

constexpr EventBits_t START_BIT = BIT0;
constexpr EventBits_t DONE_BITS[2] = {BIT1, BIT2};

volatile uint32_t _sink;  // keeps the compiler from deleting the read loops

struct ReadJob {
    const uint32_t* words;
    size_t count;
    EventGroupHandle_t events;
    EventBits_t doneBit;
};

uint32_t readWords(const uint32_t* p, size_t words) {
    uint32_t a = 0, b = 0, c = 0, d = 0;
    for (size_t i = 0; i + 4 <= words; i += 4) {
        a += p[i];
        b += p[i + 1];
        c += p[i + 2];
        d += p[i + 3];
    }
    return a ^ b ^ c ^ d;
}

// bytes per microsecond is the same number as MB/s
float mbps(size_t bytes, int64_t micros) {
    return micros > 0 ? static_cast<float>(bytes) / static_cast<float>(micros) : 0.0f;
}

float timeReads(const void* buf, size_t bytes, int passes) {
    const uint32_t* p = static_cast<const uint32_t*>(buf);
    _sink = readWords(p, bytes / 4);  // warm-up
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < passes; i++) {
        _sink = readWords(p, bytes / 4);
    }
    return mbps(bytes * passes, esp_timer_get_time() - t0);
}

void readTask(void* arg) {
    const ReadJob* job = static_cast<const ReadJob*>(arg);
    xEventGroupWaitBits(job->events, START_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    for (int i = 0; i < PSRAM_PASSES; i++) {
        _sink = readWords(job->words, job->count);
    }
    xEventGroupSetBits(job->events, job->doneBit);
    vTaskDelete(nullptr);
}

// One reader pinned to each core, released by a single bit so they start together.
float timeDualCoreReads(const void* buf, size_t bytes) {
    EventGroupHandle_t events = xEventGroupCreate();
    if (events == nullptr) {
        return 0.0f;
    }
    const uint32_t* p = static_cast<const uint32_t*>(buf);
    const size_t wordsPerCore = bytes / 8;
    ReadJob jobs[2] = {
        {p, wordsPerCore, events, DONE_BITS[0]},
        {p + wordsPerCore, wordsPerCore, events, DONE_BITS[1]},
    };
    for (int core = 0; core < 2; core++) {
        xTaskCreatePinnedToCore(readTask, "memread", 4096, &jobs[core], READER_PRIORITY, nullptr, core);
    }
    int64_t t0 = esp_timer_get_time();
    xEventGroupSetBits(events, START_BIT);
    xEventGroupWaitBits(events, DONE_BITS[0] | DONE_BITS[1], pdFALSE, pdTRUE, portMAX_DELAY);
    int64_t elapsed = esp_timer_get_time() - t0;
    vTaskDelay(pdMS_TO_TICKS(10));  // let both readers leave xEventGroupSetBits before the group goes away
    vEventGroupDelete(events);
    return mbps(bytes * PSRAM_PASSES, elapsed);
}

float timeWrites(void* buf, size_t bytes, int passes) {
    int64_t t0 = esp_timer_get_time();
    for (int i = 0; i < passes; i++) {
        memset(buf, 0x55 + i, bytes);
    }
    return mbps(bytes * passes, esp_timer_get_time() - t0);
}

// Maps the start of the running app partition and reads it through the data cache.
float timeFlashReads() {
    const esp_partition_t* part = esp_ota_get_running_partition();
    if (part == nullptr || part->size < FLASH_MAP_BYTES) {
        return 0.0f;
    }
    const void* ptr = nullptr;
    spi_flash_mmap_handle_t handle;
    if (esp_partition_mmap(part, 0, FLASH_MAP_BYTES, SPI_FLASH_MMAP_DATA, &ptr, &handle) != ESP_OK) {
        return 0.0f;
    }
    float result = timeReads(ptr, FLASH_MAP_BYTES, PSRAM_PASSES);
    spi_flash_munmap(handle);
    return result;
}

}  // namespace

MemBenchResults runMemBench() {
    MemBenchResults r = {};

    void* sram = heap_caps_malloc(SRAM_BUF_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (sram != nullptr) {
        memset(sram, 0xA5, SRAM_BUF_BYTES);
        r.sramRead = timeReads(sram, SRAM_BUF_BYTES, SRAM_PASSES);
        heap_caps_free(sram);
    }

    void* psram = heap_caps_malloc(PSRAM_BUF_BYTES, MALLOC_CAP_SPIRAM);
    if (psram != nullptr) {
        r.psramOk = true;
        r.psramWrite = timeWrites(psram, PSRAM_BUF_BYTES, PSRAM_PASSES);
        r.psramRead = timeReads(psram, PSRAM_BUF_BYTES, PSRAM_PASSES);
        r.psramReadDualCore = timeDualCoreReads(psram, PSRAM_BUF_BYTES);
        heap_caps_free(psram);
    }

    r.flashRead = timeFlashReads();
    return r;
}
