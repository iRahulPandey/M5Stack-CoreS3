#include <math.h>
#include <unity.h>

#include "motion.h"

using drama::MotionClassifier;
using drama::ShakeLevel;

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

void test_should_explain_each_hit() {
    MotionClassifier m;
    rest(m, 1000);
    hit(m, 2.5f, 2);
    rest(m, 1000);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().hits);
    TEST_ASSERT_EQUAL(drama::HitVerdict::Slap, m.state().lastHitVerdict);
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 2.4f, m.state().lastHitPeak);

    hit(m, 1.5f, 40);  // a 400 ms push is movement, not a hit (letting go is another long jolt)
    rest(m, 1000);
    TEST_ASSERT_TRUE(m.state().hits >= 2);
    TEST_ASSERT_EQUAL(drama::HitVerdict::TooLong, m.state().lastHitVerdict);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().slaps);
    TEST_ASSERT_EQUAL_STRING("too_long", drama::hitVerdictName(m.state().lastHitVerdict));
}

// Real slaps on the board peaked at 2.4 to 7.2 g, but the cube kept rocking (or was held) for
// hundreds of ms afterwards, so "a short, isolated jolt" missed every one of them.
void test_should_count_a_slap_even_when_the_cube_keeps_rocking() {
    MotionClassifier m;
    rest(m, 1000);
    hit(m, 3.0f, 2);
    for (uint32_t t = 0; t < 600; t += STEP_MS, now += STEP_MS) {  // rocking, dying away
        m.update(0.6f * expf(-t / 300.0f) * sinf(2.0f * PI_F * 8.0f * t / 1000.0f), 0.0f, 1.0f, now);
    }
    rest(m, 500);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().slaps);
}

void test_should_count_a_slap_right_after_other_handling() {
    MotionClassifier m;
    rest(m, 1000);
    hit(m, 0.6f, 2);
    rest(m, 200);
    hit(m, 2.5f, 2);
    rest(m, 800);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().slaps);
}

void test_should_not_mistake_the_landing_thud_for_a_slap() {
    MotionClassifier m;
    rest(m, 1000, 0.0f, 1.0f, 0.0f);
    for (int i = 0; i < 15; i++, now += STEP_MS) m.update(0.0f, 0.05f, 0.0f, now);  // in the air
    for (int i = 0; i < 2; i++, now += STEP_MS) m.update(0.0f, 3.5f, 0.0f, now);    // thud
    rest(m, 800, 0.0f, 1.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().landings);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().slaps);
}

// Dropping or tossing it: the accelerometer reads close to 0 g while it is in the air.
void fall(MotionClassifier& m, int samples) {
    for (int i = 0; i < samples; i++, now += STEP_MS) m.update(0.0f, 0.05f, 0.0f, now);
}

void test_should_notice_a_toss_and_the_landing() {
    MotionClassifier m;
    rest(m, 1000, 0.0f, 1.0f, 0.0f);
    fall(m, 15);
    TEST_ASSERT_TRUE(m.state().falling);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().falls);
    for (int i = 0; i < 2; i++, now += STEP_MS) m.update(0.0f, 3.0f, 0.0f, now);  // thud
    rest(m, 500, 0.0f, 1.0f, 0.0f);
    TEST_ASSERT_FALSE(m.state().falling);
    TEST_ASSERT_EQUAL_UINT32(1, m.state().landings);
}

void test_should_ignore_a_dip_too_short_to_be_a_fall() {
    MotionClassifier m;
    rest(m, 1000, 0.0f, 1.0f, 0.0f);
    fall(m, 7);  // 70 ms: a quick move of the hand, not a toss
    rest(m, 500, 0.0f, 1.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().falls);
}

void test_should_know_when_it_lies_face_down_or_on_its_back() {
    MotionClassifier m;
    rest(m, 1000, 0.0f, 1.0f, 0.0f);  // standing up
    TEST_ASSERT_FALSE(m.state().faceDown);
    TEST_ASSERT_FALSE(m.state().onBack);
    rest(m, 1000, 0.0f, 0.0f, -1.0f);  // screen towards the desk
    TEST_ASSERT_TRUE(m.state().faceDown);
    rest(m, 1000, 0.0f, 0.0f, 1.0f);  // screen towards the ceiling
    TEST_ASSERT_FALSE(m.state().faceDown);
    TEST_ASSERT_TRUE(m.state().onBack);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, m.state().gravityZ);
}

void test_should_notice_being_picked_up_after_resting() {
    MotionClassifier m;
    rest(m, 4000, 0.0f, 1.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT32(0, m.state().pickups);
    for (uint32_t t = 0; t < 1000; t += STEP_MS, now += STEP_MS) {
        m.update(0.3f * sinf(2.0f * PI_F * 2.0f * t / 1000.0f), 1.0f, 0.0f, now);
    }
    TEST_ASSERT_EQUAL_UINT32(1, m.state().pickups);
}

void test_should_not_count_a_pickup_without_resting_first() {
    MotionClassifier m;
    rest(m, 1000, 0.0f, 1.0f, 0.0f);
    for (uint32_t t = 0; t < 1000; t += STEP_MS, now += STEP_MS) {
        m.update(0.3f * sinf(2.0f * PI_F * 2.0f * t / 1000.0f), 1.0f, 0.0f, now);
    }
    TEST_ASSERT_EQUAL_UINT32(0, m.state().pickups);
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
    RUN_TEST(test_should_explain_each_hit);
    RUN_TEST(test_should_count_a_slap_even_when_the_cube_keeps_rocking);
    RUN_TEST(test_should_count_a_slap_right_after_other_handling);
    RUN_TEST(test_should_not_mistake_the_landing_thud_for_a_slap);
    RUN_TEST(test_should_notice_a_toss_and_the_landing);
    RUN_TEST(test_should_ignore_a_dip_too_short_to_be_a_fall);
    RUN_TEST(test_should_know_when_it_lies_face_down_or_on_its_back);
    RUN_TEST(test_should_notice_being_picked_up_after_resting);
    RUN_TEST(test_should_not_count_a_pickup_without_resting_first);
    return UNITY_END();
}
