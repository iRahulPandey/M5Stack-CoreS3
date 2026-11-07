#include <unity.h>

#include "gravity.h"

using sand::Gravity;
using sand::Pacer;

namespace {

void assertDown(int dx, int dy, const Gravity& g) {
    TEST_ASSERT_EQUAL_INT(dx, g.down.dx);
    TEST_ASSERT_EQUAL_INT(dy, g.down.dy);
}

int runFor(Pacer& p, float strength, float seconds) {
    int total = 0;
    for (float t = 0.0f; t < seconds; t += 0.033f) total += p.steps(strength, 0.033f);
    return total;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_should_fall_down_the_screen_when_standing_up() {
    Gravity g = sand::gravityFrom(0.0f, 1.0f, 0.0f);
    assertDown(0, 1, g);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, g.strength);
}

void test_should_fall_up_the_screen_when_upside_down() {
    assertDown(0, -1, sand::gravityFrom(0.0f, -1.0f, 0.0f));
}

// Lowering the right side tips the up-pointing reading towards the left.
void test_should_fall_right_when_lying_on_its_right_side() {
    assertDown(1, 0, sand::gravityFrom(-1.0f, 0.0f, 0.0f));
}

void test_should_fall_diagonally_once_leaned_far_enough() {
    assertDown(1, 1, sand::gravityFrom(-0.707f, 0.707f, 0.0f));  // 45 degrees
    assertDown(1, 1, sand::gravityFrom(-0.5f, 0.866f, 0.0f));    // 30 degrees: nearer diagonal than straight
    assertDown(0, 1, sand::gravityFrom(-0.342f, 0.94f, 0.0f));   // 20 degrees: still straight down
}

void test_should_feel_weaker_gravity_as_it_lies_back() {
    Gravity flat = sand::gravityFrom(0.0f, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, flat.strength);
    Gravity leaning = sand::gravityFrom(0.0f, 0.5f, 0.866f);
    assertDown(0, 1, leaning);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, leaning.strength);
}

void test_should_ignore_how_hard_the_board_is_moved() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, sand::gravityFrom(0.0f, 2.0f, 0.0f).strength);
}

void test_should_have_no_down_in_free_fall() {
    Gravity g = sand::gravityFrom(0.0f, 0.0f, 0.0f);
    assertDown(0, 0, g);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, g.strength);
}

void test_should_step_fastest_upright_and_not_at_all_lying_flat() {
    Pacer upright, half, nearlyFlat;
    int full = runFor(upright, 1.0f, 1.0f);
    TEST_ASSERT_INT_WITHIN(3, 75, full);
    TEST_ASSERT_INT_WITHIN(3, full / 2, runFor(half, 0.5f, 1.0f));
    TEST_ASSERT_EQUAL_INT(0, runFor(nearlyFlat, 0.15f, 1.0f));
}

void test_should_not_try_to_catch_up_after_a_stalled_frame() {
    Pacer p;
    TEST_ASSERT_EQUAL_INT(4, p.steps(1.0f, 2.0f));
    TEST_ASSERT_TRUE(p.steps(1.0f, 0.033f) <= 3);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_fall_down_the_screen_when_standing_up);
    RUN_TEST(test_should_fall_up_the_screen_when_upside_down);
    RUN_TEST(test_should_fall_right_when_lying_on_its_right_side);
    RUN_TEST(test_should_fall_diagonally_once_leaned_far_enough);
    RUN_TEST(test_should_feel_weaker_gravity_as_it_lies_back);
    RUN_TEST(test_should_ignore_how_hard_the_board_is_moved);
    RUN_TEST(test_should_have_no_down_in_free_fall);
    RUN_TEST(test_should_step_fastest_upright_and_not_at_all_lying_flat);
    RUN_TEST(test_should_not_try_to_catch_up_after_a_stalled_frame);
    return UNITY_END();
}
