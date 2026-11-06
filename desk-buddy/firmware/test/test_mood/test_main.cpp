#include <string.h>
#include <unity.h>

#include "mood.h"

using buddy::Mood;
using buddy::MoodEngine;
using buddy::MoodInputs;
using buddy::MoodTimings;
using buddy::ShakeLevel;

namespace {

constexpr uint32_t STEP_MS = 50;

uint32_t now = 0;

// Feeds the same input for `ms`, one update per step.
void hold(MoodEngine& e, const MoodInputs& in, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += STEP_MS, now += STEP_MS) e.update(in, now);
}

void wait(MoodEngine& e, uint32_t ms) {
    hold(e, MoodInputs(), ms);
}

void once(MoodEngine& e, const MoodInputs& in) {
    e.update(in, now);
    now += STEP_MS;
}

MoodInputs slap() { MoodInputs in; in.slap = true; return in; }
MoodInputs shaking(ShakeLevel level) { MoodInputs in; in.shake = level; return in; }

}  // namespace

void setUp() {
    now = 1000;
}

void tearDown() {}

void test_should_start_idle() {
    MoodEngine e;
    TEST_ASSERT_EQUAL(Mood::Idle, e.mood());
}

void test_should_get_bored_then_fall_asleep_when_left_alone() {
    MoodTimings t;
    MoodEngine e(t);
    wait(e, t.boredAfterMs + STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Bored, e.mood());
    wait(e, t.asleepAfterMs - t.boredAfterMs);
    TEST_ASSERT_EQUAL(Mood::Asleep, e.mood());
}

void test_should_wake_up_surprised() {
    MoodTimings t;
    MoodEngine e(t);
    wait(e, t.asleepAfterMs + STEP_MS);
    MoodInputs poke;
    poke.poke = true;
    once(e, poke);
    TEST_ASSERT_EQUAL(Mood::Surprised, e.mood());
}

void test_should_get_angry_when_slapped_then_calm_down() {
    MoodTimings t;
    MoodEngine e(t);
    once(e, slap());
    TEST_ASSERT_EQUAL(Mood::Angry, e.mood());
    wait(e, t.angryMs - 2 * STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Angry, e.mood());
    wait(e, 2 * STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Idle, e.mood());
}

void test_should_stay_angry_longer_after_a_second_slap() {
    MoodTimings t;
    MoodEngine e(t);
    once(e, slap());
    wait(e, 1000);
    once(e, slap());
    TEST_ASSERT_EQUAL_UINT8(2, e.grumpiness());
    wait(e, t.angryMs + 1000);
    TEST_ASSERT_EQUAL(Mood::Angry, e.mood());
}

void test_should_be_happy_after_a_gentle_shake() {
    MoodTimings t;
    MoodEngine e(t);
    hold(e, shaking(ShakeLevel::Gentle), 500);
    TEST_ASSERT_EQUAL(Mood::Happy, e.mood());
    wait(e, t.happyMs + STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Idle, e.mood());
}

void test_should_be_excited_then_happy_then_idle_after_a_medium_shake() {
    MoodTimings t;
    MoodEngine e(t);
    hold(e, shaking(ShakeLevel::Medium), 500);
    TEST_ASSERT_EQUAL(Mood::Excited, e.mood());
    wait(e, t.excitedLingerMs + STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Happy, e.mood());
    wait(e, t.happyMs + STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Idle, e.mood());
}

void test_should_get_dizzy_from_a_long_hard_shake_and_stay_dizzy() {
    MoodTimings t;
    MoodEngine e(t);
    hold(e, shaking(ShakeLevel::Hard), t.dizzyAfterHardMs + STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Dizzy, e.mood());
    wait(e, t.dizzyMs - 2 * STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Dizzy, e.mood());
}

void test_should_only_be_excited_by_a_short_hard_shake() {
    MoodTimings t;
    MoodEngine e(t);
    hold(e, shaking(ShakeLevel::Hard), t.dizzyAfterHardMs / 2);
    TEST_ASSERT_EQUAL(Mood::Excited, e.mood());
}

void test_should_feel_loved_while_stroked_and_curious_when_a_hand_is_near() {
    MoodEngine e;
    MoodInputs stroke;
    stroke.stroking = true;
    hold(e, stroke, 300);
    TEST_ASSERT_EQUAL(Mood::Loved, e.mood());

    MoodEngine e2;
    MoodInputs hand;
    hand.handNear = true;
    hold(e2, hand, 300);
    TEST_ASSERT_EQUAL(Mood::Curious, e2.mood());
}

void test_should_be_surprised_by_a_bump() {
    MoodEngine e;
    MoodInputs bump;
    bump.tap = true;
    once(e, bump);
    TEST_ASSERT_EQUAL(Mood::Surprised, e.mood());
}

void test_should_let_a_slap_interrupt_happiness() {
    MoodEngine e;
    hold(e, shaking(ShakeLevel::Gentle), 300);
    once(e, slap());
    TEST_ASSERT_EQUAL(Mood::Angry, e.mood());
}

void test_should_hold_a_forced_mood_for_its_usual_time() {
    MoodTimings t;
    MoodEngine e(t);
    e.force(Mood::Dizzy, now);
    wait(e, t.dizzyMs - 2 * STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Dizzy, e.mood());
    wait(e, 3 * STEP_MS);
    TEST_ASSERT_EQUAL(Mood::Idle, e.mood());
}

void test_should_round_trip_mood_names() {
    for (int i = 0; i < static_cast<int>(Mood::Count); i++) {
        Mood parsed = Mood::Idle;
        TEST_ASSERT_TRUE(buddy::moodFromName(buddy::moodName(static_cast<Mood>(i)), parsed));
        TEST_ASSERT_EQUAL(i, static_cast<int>(parsed));
    }
    Mood ignored;
    TEST_ASSERT_FALSE(buddy::moodFromName("grumpy-cat", ignored));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_start_idle);
    RUN_TEST(test_should_get_bored_then_fall_asleep_when_left_alone);
    RUN_TEST(test_should_wake_up_surprised);
    RUN_TEST(test_should_get_angry_when_slapped_then_calm_down);
    RUN_TEST(test_should_stay_angry_longer_after_a_second_slap);
    RUN_TEST(test_should_be_happy_after_a_gentle_shake);
    RUN_TEST(test_should_be_excited_then_happy_then_idle_after_a_medium_shake);
    RUN_TEST(test_should_get_dizzy_from_a_long_hard_shake_and_stay_dizzy);
    RUN_TEST(test_should_only_be_excited_by_a_short_hard_shake);
    RUN_TEST(test_should_feel_loved_while_stroked_and_curious_when_a_hand_is_near);
    RUN_TEST(test_should_be_surprised_by_a_bump);
    RUN_TEST(test_should_let_a_slap_interrupt_happiness);
    RUN_TEST(test_should_hold_a_forced_mood_for_its_usual_time);
    RUN_TEST(test_should_round_trip_mood_names);
    return UNITY_END();
}
