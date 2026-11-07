#include <math.h>
#include <unity.h>

#include "shake.h"

using sand::ShakeDetector;

namespace {

constexpr float PI_F = 3.14159265f;
constexpr int SAMPLES_PER_S = 100;

void rest(ShakeDetector& d, float seconds) {
    for (int i = 0; i < seconds * SAMPLES_PER_S; i++) d.update(0.0f, 1.0f, 0.0f);
}

void wiggle(ShakeDetector& d, float amplitude, float hz, float seconds) {
    for (int i = 0; i < seconds * SAMPLES_PER_S; i++) {
        float t = static_cast<float>(i) / SAMPLES_PER_S;
        d.update(amplitude * sinf(2.0f * PI_F * hz * t), 1.0f, 0.0f);
    }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_should_not_shake_at_rest() {
    ShakeDetector d;
    rest(d, 2.0f);
    TEST_ASSERT_FALSE(d.shaking());
}

void test_should_notice_a_real_shake_and_its_end() {
    ShakeDetector d;
    rest(d, 0.5f);
    wiggle(d, 1.5f, 4.0f, 1.0f);
    TEST_ASSERT_TRUE(d.shaking());
    rest(d, 1.5f);
    TEST_ASSERT_FALSE(d.shaking());
}

void test_should_not_mistake_gentle_handling_for_a_shake() {
    ShakeDetector d;
    rest(d, 0.5f);
    wiggle(d, 0.2f, 2.0f, 2.0f);
    TEST_ASSERT_FALSE(d.shaking());
}

// Turning it over to pour the sand the other way is a tilt, not a shake.
void test_should_not_mistake_turning_it_over_for_a_shake() {
    ShakeDetector d;
    rest(d, 0.5f);
    for (int i = 0; i < 2 * SAMPLES_PER_S; i++) {  // a half turn in 2 s
        float angle = PI_F * i / (2.0f * SAMPLES_PER_S);
        d.update(sinf(angle), cosf(angle), 0.0f);
    }
    TEST_ASSERT_FALSE(d.shaking());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_not_shake_at_rest);
    RUN_TEST(test_should_notice_a_real_shake_and_its_end);
    RUN_TEST(test_should_not_mistake_gentle_handling_for_a_shake);
    RUN_TEST(test_should_not_mistake_turning_it_over_for_a_shake);
    return UNITY_END();
}
