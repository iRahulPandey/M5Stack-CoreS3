#include <math.h>
#include <unity.h>

#include "motion.h"

using buddy::MotionClassifier;
using buddy::ShakeLevel;

namespace {

constexpr uint32_t STEP_MS = 10;
constexpr float PI_F = 3.14159265f;

uint32_t now = 0;

void rest(MotionClassifier& m, uint32_t ms, float gx = 0.0f, float gy = 0.0f, float gz = 1.0f) {
    for (uint32_t t = 0; t < ms; t += STEP_MS, now += STEP_MS) m.update(gx, gy, gz, now);
}

// A hit: `samples` readings of `peak` g along x on top of gravity.
void hit(MotionClassifier& m, float peak, int samples) {
    for (int i = 0; i < samples; i++, now += STEP_MS) m.update(peak, 0.0f, 1.0f, now);
}

void shake(MotionClassifier& m, float amplitude, float hz, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += STEP_MS, now += STEP_MS) {
        m.update(amplitude * sinf(2.0f * PI_F * hz * t / 1000.0f), 0.0f, 1.0f, now);
    }
}

}  // namespace

void setUp() {
    now = 0;
}

void tearDown() {}

void test_should_stay_still_at_rest() {
    MotionClassifier m;
    rest(m, 2000);
    TEST_ASSERT_EQUAL(ShakeLevel::None, m.state().shake);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().slaps);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().taps);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.0f, m.state().energy);
}

void test_should_count_one_hard_isolated_hit_as_a_slap() {
    MotionClassifier m;
    rest(m, 1000);
    hit(m, 2.5f, 2);
    rest(m, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().slaps);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().taps);
}

void test_should_count_a_light_isolated_hit_as_a_tap() {
    MotionClassifier m;
    rest(m, 1000);
    hit(m, 0.8f, 2);
    rest(m, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().taps);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().slaps);
}

void test_should_count_two_slaps_a_second_apart() {
    MotionClassifier m;
    rest(m, 1000);
    hit(m, 2.5f, 2);
    rest(m, 1000);
    hit(m, 3.0f, 1);
    rest(m, 1000);
    TEST_ASSERT_EQUAL_UINT32(2, m.state().slaps);
}

void test_should_read_a_gentle_shake_as_gentle_without_slaps() {
    MotionClassifier m;
    rest(m, 500);
    shake(m, 0.5f, 3.0f, 1500);
    TEST_ASSERT_EQUAL(ShakeLevel::Gentle, m.state().shake);
    rest(m, 1500);
    TEST_ASSERT_EQUAL(ShakeLevel::None, m.state().shake);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().slaps);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().taps);
}

void test_should_read_a_medium_shake_as_medium() {
    MotionClassifier m;
    rest(m, 500);
    shake(m, 1.2f, 4.0f, 1500);
    TEST_ASSERT_EQUAL(ShakeLevel::Medium, m.state().shake);
}

void test_should_read_a_hard_shake_as_hard_without_slaps() {
    MotionClassifier m;
    rest(m, 500);
    shake(m, 2.5f, 5.0f, 1500);
    TEST_ASSERT_EQUAL(ShakeLevel::Hard, m.state().shake);
    rest(m, 1500);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().slaps);
}

void test_should_report_tilt_from_gravity() {
    MotionClassifier m;
    rest(m, 2000, 0.5f, 0.0f, 0.866f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, m.state().tiltX);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, m.state().tiltY);
}

void test_should_notice_a_tilt_and_get_used_to_it() {
    MotionClassifier m;
    rest(m, 2000, 0.0f, 1.0f, 0.0f);  // standing upright: gravity down the screen
    TEST_ASSERT_FALSE(m.state().tilted);
    rest(m, 1000, 0.5f, 0.866f, 0.0f);  // leaned 30 degrees to the right
    TEST_ASSERT_TRUE(m.state().tilted);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.5f, m.state().leanX);
    rest(m, 120000, 0.5f, 0.866f, 0.0f);  // left like that: the new normal
    TEST_ASSERT_FALSE(m.state().tilted);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_stay_still_at_rest);
    RUN_TEST(test_should_count_one_hard_isolated_hit_as_a_slap);
    RUN_TEST(test_should_count_a_light_isolated_hit_as_a_tap);
    RUN_TEST(test_should_count_two_slaps_a_second_apart);
    RUN_TEST(test_should_read_a_gentle_shake_as_gentle_without_slaps);
    RUN_TEST(test_should_read_a_medium_shake_as_medium);
    RUN_TEST(test_should_read_a_hard_shake_as_hard_without_slaps);
    RUN_TEST(test_should_report_tilt_from_gravity);
    RUN_TEST(test_should_notice_a_tilt_and_get_used_to_it);
    return UNITY_END();
}
