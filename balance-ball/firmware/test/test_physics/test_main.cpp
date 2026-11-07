#include <unity.h>

#include "physics.h"

using ball::Arena;
using ball::Ball;
using ball::Vec2;

namespace {

constexpr float FRAME_S = 0.033f;  // about one frame on the board
const Arena ARENA;                 // 320 x 240, ball radius 34

Ball rollFor(Ball b, Vec2 pull, float seconds) {
    for (float t = 0.0f; t < seconds; t += FRAME_S) b = ball::roll(b, ARENA, pull, FRAME_S);
    return b;
}

Ball offCenter(float dx, float dy) {
    Ball b = ball::centered(ARENA);
    b.x += dx;
    b.y += dy;
    return b;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_should_start_in_the_middle_far_from_the_walls() {
    Ball b = ball::centered(ARENA);
    TEST_ASSERT_EQUAL_FLOAT(160.0f, b.x);
    TEST_ASSERT_EQUAL_FLOAT(120.0f, b.y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ball::fromCenter(b, ARENA));
    TEST_ASSERT_EQUAL_FLOAT(86.0f, ball::wallGap(b, ARENA));
    TEST_ASSERT_EQUAL_UINT8(0, ball::wallsTouched(b, ARENA));
}

void test_should_stay_in_the_middle_while_level() {
    Ball b = rollFor(ball::centered(ARENA), Vec2{}, 3.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ball::fromCenter(b, ARENA));
}

void test_should_roll_the_way_the_board_leans() {
    Ball right = rollFor(ball::centered(ARENA), Vec2{0.2f, 0.0f}, 0.3f);
    TEST_ASSERT_GREATER_THAN_FLOAT(170.0f, right.x);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 120.0f, right.y);

    Ball up = rollFor(ball::centered(ARENA), Vec2{0.0f, -0.2f}, 0.3f);
    TEST_ASSERT_LESS_THAN_FLOAT(110.0f, up.y);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 160.0f, up.x);
}

void test_should_slowly_drift_off_the_middle_when_left_alone() {
    Ball b = rollFor(offCenter(3.0f, 0.0f), Vec2{}, 1.0f);
    TEST_ASSERT_GREATER_THAN_FLOAT(163.5f, b.x);
}

// The hill is there to keep the player busy, not to win on its own.
void test_should_give_the_player_time_to_catch_a_drift() {
    Ball b = offCenter(3.0f, 0.0f);
    float t = 0.0f;
    while (ball::wallsTouched(b, ARENA) == 0 && t < 30.0f) {
        b = ball::roll(b, ARENA, Vec2{}, FRAME_S);
        t += FRAME_S;
    }
    TEST_ASSERT_GREATER_THAN_FLOAT(2.5f, t);
    TEST_ASSERT_LESS_THAN_FLOAT(30.0f, t);
}

void test_should_rest_against_the_wall_it_is_tilted_into() {
    Ball b = rollFor(ball::centered(ARENA), Vec2{0.5f, 0.0f}, 3.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 320.0f - 34.0f, b.x);
    TEST_ASSERT_EQUAL_UINT8(ball::WALL_RIGHT, ball::wallsTouched(b, ARENA));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, ball::wallGap(b, ARENA));
}

void test_should_lose_speed_when_it_bounces() {
    Ball b = offCenter(116.0f, 0.0f);
    b.vx = 300.0f;
    b = rollFor(b, Vec2{}, 0.1f);
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, b.vx);
    TEST_ASSERT_LESS_THAN_FLOAT(150.0f, -b.vx);
}

void test_should_never_leave_the_screen_even_in_one_long_step() {
    Ball b = ball::roll(ball::centered(ARENA), ARENA, Vec2{1.0f, 1.0f}, 2.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 320.0f - 34.0f, b.x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 240.0f - 34.0f, b.y);
    TEST_ASSERT_EQUAL_UINT8(ball::WALL_RIGHT | ball::WALL_BOTTOM, ball::wallsTouched(b, ARENA));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_start_in_the_middle_far_from_the_walls);
    RUN_TEST(test_should_stay_in_the_middle_while_level);
    RUN_TEST(test_should_roll_the_way_the_board_leans);
    RUN_TEST(test_should_slowly_drift_off_the_middle_when_left_alone);
    RUN_TEST(test_should_give_the_player_time_to_catch_a_drift);
    RUN_TEST(test_should_rest_against_the_wall_it_is_tilted_into);
    RUN_TEST(test_should_lose_speed_when_it_bounces);
    RUN_TEST(test_should_never_leave_the_screen_even_in_one_long_step);
    return UNITY_END();
}
