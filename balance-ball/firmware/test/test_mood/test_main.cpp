#include <string.h>
#include <unity.h>

#include "mood.h"

using ball::Mood;
using ball::MoodEngine;
using ball::Situation;

namespace {

constexpr uint32_t FRAME_MS = 33;
uint32_t now = 0;

const Situation MIDDLE{0.0f, 86.0f, false};
const Situation A_LITTLE_OFF{40.0f, 54.0f, false};
const Situation NEAR_A_WALL{70.0f, 30.0f, false};
const Situation RIGHT_BY_A_WALL{78.0f, 8.0f, false};
const Situation AGAINST_A_WALL{86.0f, 0.0f, true};

void feel(MoodEngine& engine, const Situation& s, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += FRAME_MS, now += FRAME_MS) engine.update(s, now);
}

}  // namespace

void setUp() {
    now = 1000;
}

void tearDown() {}

void test_should_be_happy_in_the_middle() {
    MoodEngine e;
    feel(e, MIDDLE, 500);
    TEST_ASSERT_EQUAL(Mood::Happy, e.mood());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, e.redness());
}

void test_should_grow_blissful_while_it_stays_in_the_middle() {
    MoodEngine e;
    feel(e, MIDDLE, 3500);
    TEST_ASSERT_EQUAL(Mood::Blissful, e.mood());
}

void test_should_be_content_a_little_off_the_middle() {
    MoodEngine e;
    feel(e, A_LITTLE_OFF, 500);
    TEST_ASSERT_EQUAL(Mood::Content, e.mood());
}

void test_should_worry_as_a_wall_gets_close_and_panic_right_by_it() {
    MoodEngine e;
    feel(e, NEAR_A_WALL, 500);
    TEST_ASSERT_EQUAL(Mood::Worried, e.mood());
    feel(e, RIGHT_BY_A_WALL, 500);
    TEST_ASSERT_EQUAL(Mood::Panicking, e.mood());
}

void test_should_get_angry_the_moment_it_touches_a_wall_and_turn_red() {
    MoodEngine e;
    feel(e, MIDDLE, 500);
    e.update(AGAINST_A_WALL, now);
    TEST_ASSERT_EQUAL(Mood::Angry, e.mood());
    feel(e, AGAINST_A_WALL, 300);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, e.redness());
}

void test_should_fume_when_held_against_the_wall() {
    MoodEngine e;
    feel(e, AGAINST_A_WALL, 2000);
    TEST_ASSERT_EQUAL(Mood::Fuming, e.mood());
}

void test_should_stay_angry_a_moment_then_sulk_until_the_red_fades() {
    MoodEngine e;
    feel(e, AGAINST_A_WALL, 500);
    feel(e, MIDDLE, 1000);
    TEST_ASSERT_EQUAL(Mood::Angry, e.mood());
    feel(e, MIDDLE, 1000);
    TEST_ASSERT_EQUAL(Mood::Sulking, e.mood());
    float sulkingRed = e.redness();
    TEST_ASSERT_TRUE(sulkingRed > 0.0f && sulkingRed < 1.0f);
    feel(e, MIDDLE, 4000);
    TEST_ASSERT_EQUAL(Mood::Blissful, e.mood());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, e.redness());
}

void test_should_not_flicker_when_it_brushes_past_a_boundary() {
    MoodEngine e;
    feel(e, A_LITTLE_OFF, 500);
    feel(e, NEAR_A_WALL, FRAME_MS);
    feel(e, A_LITTLE_OFF, 500);
    TEST_ASSERT_EQUAL(Mood::Content, e.mood());
    feel(e, NEAR_A_WALL, 400);
    TEST_ASSERT_EQUAL(Mood::Worried, e.mood());
}

// On the board, a ball racing from the middle to a wall still looked blissful 5 px from it: every
// zone it crossed restarted the wait, so no new mood ever got to show.
void test_should_keep_up_with_a_ball_racing_towards_a_wall() {
    MoodEngine e;
    feel(e, MIDDLE, 500);
    feel(e, A_LITTLE_OFF, 100);
    feel(e, NEAR_A_WALL, 100);
    feel(e, RIGHT_BY_A_WALL, 150);
    TEST_ASSERT_EQUAL(Mood::Panicking, e.mood());
}

void test_should_time_its_stay_in_the_middle_and_keep_the_best() {
    MoodEngine e;
    feel(e, MIDDLE, 2000);
    TEST_ASSERT_UINT32_WITHIN(50, 2000, e.streakMs(now));
    feel(e, A_LITTLE_OFF, 500);
    TEST_ASSERT_EQUAL_UINT32(0, e.streakMs(now));
    TEST_ASSERT_UINT32_WITHIN(50, 2000, e.bestMs());
    feel(e, MIDDLE, 1000);
    TEST_ASSERT_UINT32_WITHIN(50, 1000, e.streakMs(now));
    TEST_ASSERT_UINT32_WITHIN(50, 2000, e.bestMs());
}

void test_should_count_each_bump_once() {
    MoodEngine e;
    feel(e, AGAINST_A_WALL, 500);
    TEST_ASSERT_EQUAL_UINT32(1, e.bumps());
    feel(e, NEAR_A_WALL, 200);
    feel(e, AGAINST_A_WALL, 200);
    TEST_ASSERT_EQUAL_UINT32(2, e.bumps());
}

void test_should_name_every_mood() {
    const Mood all[] = {Mood::Blissful, Mood::Happy,  Mood::Content, Mood::Worried,
                        Mood::Panicking, Mood::Angry, Mood::Fuming,  Mood::Sulking};
    for (Mood a : all) {
        for (Mood b : all) {
            if (a != b) TEST_ASSERT_TRUE(strcmp(ball::moodName(a), ball::moodName(b)) != 0);
        }
    }
    TEST_ASSERT_EQUAL_STRING("fuming", ball::moodName(Mood::Fuming));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_be_happy_in_the_middle);
    RUN_TEST(test_should_grow_blissful_while_it_stays_in_the_middle);
    RUN_TEST(test_should_be_content_a_little_off_the_middle);
    RUN_TEST(test_should_worry_as_a_wall_gets_close_and_panic_right_by_it);
    RUN_TEST(test_should_get_angry_the_moment_it_touches_a_wall_and_turn_red);
    RUN_TEST(test_should_fume_when_held_against_the_wall);
    RUN_TEST(test_should_stay_angry_a_moment_then_sulk_until_the_red_fades);
    RUN_TEST(test_should_not_flicker_when_it_brushes_past_a_boundary);
    RUN_TEST(test_should_keep_up_with_a_ball_racing_towards_a_wall);
    RUN_TEST(test_should_time_its_stay_in_the_middle_and_keep_the_best);
    RUN_TEST(test_should_count_each_bump_once);
    RUN_TEST(test_should_name_every_mood);
    return UNITY_END();
}
