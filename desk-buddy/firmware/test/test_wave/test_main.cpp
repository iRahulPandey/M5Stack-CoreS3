#include <math.h>
#include <string.h>
#include <unity.h>

#include "wave.h"

using buddy::FrameMotion;
using buddy::WaveDetector;

namespace {

constexpr uint32_t FRAME_MS = 100;  // the camera delivers about 10 frames a second
constexpr float PI_F = 3.14159265f;

uint32_t now = 0;

FrameMotion moving(float x, float amount = 0.08f) {
    FrameMotion m;
    m.amount = amount;
    m.centerX = x;
    return m;
}

int countWaves(WaveDetector& d, float (*xAt)(uint32_t t), uint32_t ms, float amount = 0.08f) {
    int waves = 0;
    for (uint32_t t = 0; t < ms; t += FRAME_MS, now += FRAME_MS) {
        if (d.update(moving(xAt(t), amount), now)) waves++;
    }
    return waves;
}

float waving(uint32_t t) { return 0.5f + 0.22f * sinf(2.0f * PI_F * 1.5f * t / 1000.0f); }
float walkingAcross(uint32_t t) { return 0.1f + 0.8f * t / 2000.0f; }

}  // namespace

void setUp() {
    now = 1000;
}

void tearDown() {}

void test_should_find_motion_where_the_frame_changed() {
    uint8_t before[16], after[16];
    memset(before, 100, sizeof(before));
    memcpy(after, before, sizeof(after));
    for (int y = 0; y < 4; y++) after[y * 4 + 3] = 200;  // the right-most column brightens
    FrameMotion m = buddy::diffFrames(before, after, 4, 4, 20);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.25f, m.amount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.875f, m.centerX);
}

void test_should_ignore_changes_below_the_threshold() {
    uint8_t before[16], after[16];
    memset(before, 100, sizeof(before));
    memset(after, 110, sizeof(after));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, buddy::diffFrames(before, after, 4, 4, 20).amount);
}

void test_should_detect_a_hand_waving() {
    WaveDetector d;
    TEST_ASSERT_EQUAL_INT(1, countWaves(d, waving, 2000));
}

void test_should_not_mistake_walking_across_for_a_wave() {
    WaveDetector d;
    TEST_ASSERT_EQUAL_INT(0, countWaves(d, walkingAcross, 2000));
}

void test_should_not_count_the_whole_picture_changing() {
    WaveDetector d;
    TEST_ASSERT_EQUAL_INT(0, countWaves(d, waving, 2000, 0.85f));
}

void test_should_not_count_a_still_scene() {
    WaveDetector d;
    int waves = 0;
    for (int i = 0; i < 30; i++, now += FRAME_MS) waves += d.update(FrameMotion(), now) ? 1 : 0;
    TEST_ASSERT_EQUAL_INT(0, waves);
}

void test_should_wait_before_counting_a_second_wave() {
    WaveDetector d;
    TEST_ASSERT_EQUAL_INT(1, countWaves(d, waving, 3000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_find_motion_where_the_frame_changed);
    RUN_TEST(test_should_ignore_changes_below_the_threshold);
    RUN_TEST(test_should_detect_a_hand_waving);
    RUN_TEST(test_should_not_mistake_walking_across_for_a_wave);
    RUN_TEST(test_should_not_count_the_whole_picture_changing);
    RUN_TEST(test_should_not_count_a_still_scene);
    RUN_TEST(test_should_wait_before_counting_a_second_wave);
    return UNITY_END();
}
