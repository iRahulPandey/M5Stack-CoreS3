#include <math.h>
#include <unity.h>

#include "level.h"

using ball::Level;
using ball::Vec2;

namespace {

constexpr float HALF = 0.5f;          // sin 30 degrees
constexpr float COS_30 = 0.8660254f;

}  // namespace

void setUp() {}
void tearDown() {}

void test_should_feel_no_pull_when_level() {
    Level level;
    Vec2 p = level.pull(0.0f, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, p.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, p.y);
}

// Lowering the right side tips the up-pointing reading towards the raised left side.
void test_should_roll_right_when_the_right_side_goes_down() {
    Level level;
    Vec2 p = level.pull(-HALF, 0.0f, COS_30);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, HALF, p.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, p.y);
}

void test_should_roll_down_the_screen_when_the_bottom_edge_goes_down() {
    Level level;
    Vec2 p = level.pull(0.0f, HALF, COS_30);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, p.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, HALF, p.y);
}

void test_should_ignore_how_hard_the_board_is_moved() {
    Level level;
    Vec2 calm = level.pull(-HALF, 0.0f, COS_30);
    Vec2 jolted = level.pull(-1.5f * HALF, 0.0f, 1.5f * COS_30);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, calm.x, jolted.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, calm.y, jolted.y);
}

// Standing upright on the desk reads (0, 1, 0). Set as level, leaning it sideways rolls the ball
// sideways, and tipping its top away from you rolls the ball up the screen.
void test_should_treat_any_pose_as_level_once_set() {
    Level level;
    level.set(0.0f, 1.0f, 0.0f);
    Vec2 still = level.pull(0.0f, 1.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, still.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, still.y);

    Vec2 leaned = level.pull(-HALF, COS_30, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, HALF, leaned.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, leaned.y);

    Vec2 tippedBack = level.pull(0.0f, COS_30, HALF);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, tippedBack.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -HALF, tippedBack.y);
}

void test_should_cope_with_level_lying_on_its_side() {
    Level level;
    level.set(1.0f, 0.0f, 0.0f);
    Vec2 still = level.pull(1.0f, 0.0f, 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, still.x);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, still.y);
    Vec2 tipped = level.pull(COS_30, HALF, 0.0f);
    TEST_ASSERT_FALSE(isnan(tipped.x) || isnan(tipped.y));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, HALF, hypotf(tipped.x, tipped.y));
}

void test_should_feel_no_pull_in_free_fall() {
    Level level;
    Vec2 p = level.pull(0.0f, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.y);
}

void test_should_keep_the_old_level_when_set_with_nothing() {
    Level level;
    level.set(0.0f, 0.0f, 0.0f);
    Vec2 p = level.pull(-HALF, 0.0f, COS_30);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, HALF, p.x);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_feel_no_pull_when_level);
    RUN_TEST(test_should_roll_right_when_the_right_side_goes_down);
    RUN_TEST(test_should_roll_down_the_screen_when_the_bottom_edge_goes_down);
    RUN_TEST(test_should_ignore_how_hard_the_board_is_moved);
    RUN_TEST(test_should_treat_any_pose_as_level_once_set);
    RUN_TEST(test_should_cope_with_level_lying_on_its_side);
    RUN_TEST(test_should_feel_no_pull_in_free_fall);
    RUN_TEST(test_should_keep_the_old_level_when_set_with_nothing);
    return UNITY_END();
}
