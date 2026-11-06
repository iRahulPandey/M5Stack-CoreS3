#include <unity.h>

#include "drama.h"

using drama::DramaEngine;
using drama::Inputs;
using drama::Reaction;
using drama::ShakeLevel;
using drama::Timings;

namespace {

constexpr uint32_t STEP_MS = 50;

uint32_t now = 0;
Timings t;

void hold(DramaEngine& e, const Inputs& in, uint32_t ms) {
    for (uint32_t s = 0; s < ms; s += STEP_MS, now += STEP_MS) e.update(in, now);
}

void wait(DramaEngine& e, uint32_t ms) {
    hold(e, Inputs(), ms);
}

void once(DramaEngine& e, const Inputs& in) {
    e.update(in, now);
    now += STEP_MS;
}

Inputs slap() { Inputs in; in.slap = true; return in; }
Inputs falling() { Inputs in; in.falling = true; return in; }
Inputs landed() { Inputs in; in.landed = true; return in; }
Inputs faceDown() { Inputs in; in.faceDown = true; return in; }
Inputs onBack() { Inputs in; in.onBack = true; return in; }
Inputs tilted() { Inputs in; in.tilted = true; return in; }
Inputs shaking(ShakeLevel level) { Inputs in; in.shake = level; return in; }

}  // namespace

void setUp() {
    now = 1000;
    t = Timings();
}

void tearDown() {}

void test_should_start_idle_then_get_bored_then_sleep() {
    DramaEngine e(t);
    TEST_ASSERT_EQUAL(Reaction::Idle, e.reaction());
    wait(e, t.boredAfterMs + STEP_MS);
    TEST_ASSERT_EQUAL(Reaction::Bored, e.reaction());
    wait(e, t.asleepAfterMs - t.boredAfterMs);
    TEST_ASSERT_EQUAL(Reaction::Asleep, e.reaction());
}

void test_should_wake_up_cross() {
    DramaEngine e(t);
    wait(e, t.asleepAfterMs + STEP_MS);
    Inputs poke;
    poke.poke = true;
    once(e, poke);
    TEST_ASSERT_EQUAL(Reaction::Woken, e.reaction());
}

void test_should_be_outraged_by_a_slap_then_furious_at_the_second() {
    DramaEngine e(t);
    once(e, slap());
    TEST_ASSERT_EQUAL(Reaction::Outraged, e.reaction());
    wait(e, 2000);
    once(e, slap());
    TEST_ASSERT_EQUAL(Reaction::Furious, e.reaction());
    TEST_ASSERT_EQUAL_UINT8(2, e.slapStreak());
}

void test_should_forgive_a_slap_after_a_while() {
    DramaEngine e(t);
    once(e, slap());
    wait(e, t.furyWindowMs + 1000);
    once(e, slap());
    TEST_ASSERT_EQUAL(Reaction::Outraged, e.reaction());
}

void test_should_scream_in_the_air_and_comment_on_the_landing() {
    DramaEngine e(t);
    hold(e, falling(), 200);
    TEST_ASSERT_EQUAL(Reaction::Wheee, e.reaction());
    once(e, landed());
    TEST_ASSERT_EQUAL(Reaction::Landed, e.reaction());
    wait(e, t.landedMs + STEP_MS);
    TEST_ASSERT_EQUAL(Reaction::Idle, e.reaction());
}

void test_should_sob_face_down_and_feel_betrayed_when_turned_back() {
    DramaEngine e(t);
    hold(e, faceDown(), t.sobAfterMs / 2);
    TEST_ASSERT_EQUAL(Reaction::Idle, e.reaction());
    hold(e, faceDown(), t.sobAfterMs);
    TEST_ASSERT_EQUAL(Reaction::Sobbing, e.reaction());
    once(e, Inputs());
    TEST_ASSERT_EQUAL(Reaction::Betrayed, e.reaction());
    wait(e, t.betrayedMs + STEP_MS);
    TEST_ASSERT_EQUAL(Reaction::Idle, e.reaction());
}

void test_should_faint_on_its_back() {
    DramaEngine e(t);
    hold(e, onBack(), t.faintAfterMs + STEP_MS);
    TEST_ASSERT_EQUAL(Reaction::Fainted, e.reaction());
}

void test_should_get_queasy_only_when_held_tilted() {
    DramaEngine e(t);
    hold(e, tilted(), t.queasyAfterMs / 2);
    TEST_ASSERT_EQUAL(Reaction::Idle, e.reaction());
    hold(e, tilted(), t.queasyAfterMs);
    TEST_ASSERT_EQUAL(Reaction::Queasy, e.reaction());
}

void test_should_be_rattled_by_shaking_and_dizzy_after_a_hard_one() {
    DramaEngine e(t);
    hold(e, shaking(ShakeLevel::Medium), 300);
    TEST_ASSERT_EQUAL(Reaction::Rattled, e.reaction());
    hold(e, shaking(ShakeLevel::Hard), t.dizzyAfterHardMs + STEP_MS);
    TEST_ASSERT_EQUAL(Reaction::Dizzy, e.reaction());
    wait(e, t.dizzyMs - 2 * STEP_MS);
    TEST_ASSERT_EQUAL(Reaction::Dizzy, e.reaction());
}

void test_should_react_to_touches_and_bumps() {
    DramaEngine e(t);
    Inputs poke, stroke, bump, lift;
    poke.poke = true;
    stroke.stroking = true;
    bump.tap = true;
    lift.pickedUp = true;
    once(e, poke);
    TEST_ASSERT_EQUAL(Reaction::Tickled, e.reaction());
    hold(e, stroke, 300);
    TEST_ASSERT_EQUAL(Reaction::Smitten, e.reaction());
    once(e, bump);
    TEST_ASSERT_EQUAL(Reaction::Startled, e.reaction());
    once(e, lift);
    TEST_ASSERT_EQUAL(Reaction::PickedUp, e.reaction());
}

void test_should_let_a_slap_cut_through_anything_but_a_fall() {
    DramaEngine e(t);
    hold(e, faceDown(), t.sobAfterMs + STEP_MS);
    once(e, slap());
    TEST_ASSERT_EQUAL(Reaction::Outraged, e.reaction());

    DramaEngine e2(t);
    Inputs both = falling();
    both.slap = true;
    once(e2, both);
    TEST_ASSERT_EQUAL(Reaction::Wheee, e2.reaction());
}

void test_should_not_get_bored_while_being_handled() {
    DramaEngine e(t);
    hold(e, tilted(), t.boredAfterMs + 1000);
    TEST_ASSERT_EQUAL(Reaction::Queasy, e.reaction());
}

void test_should_hold_a_forced_reaction() {
    DramaEngine e(t);
    e.force(Reaction::Furious, now);
    wait(e, t.furiousMs - 2 * STEP_MS);
    TEST_ASSERT_EQUAL(Reaction::Furious, e.reaction());
}

void test_should_hold_a_forced_reaction_while_being_handled() {  // for filming with the cube in hand
    DramaEngine e(t);
    e.force(Reaction::Fainted, now);
    hold(e, tilted(), 3000);
    TEST_ASSERT_EQUAL(Reaction::Fainted, e.reaction());
}

void test_should_round_trip_reaction_names() {
    for (int i = 0; i < static_cast<int>(Reaction::Count); i++) {
        Reaction parsed = Reaction::Idle;
        TEST_ASSERT_TRUE(drama::reactionFromName(drama::reactionName(static_cast<Reaction>(i)), parsed));
        TEST_ASSERT_EQUAL(i, static_cast<int>(parsed));
    }
    Reaction ignored;
    TEST_ASSERT_FALSE(drama::reactionFromName("meh", ignored));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_start_idle_then_get_bored_then_sleep);
    RUN_TEST(test_should_wake_up_cross);
    RUN_TEST(test_should_be_outraged_by_a_slap_then_furious_at_the_second);
    RUN_TEST(test_should_forgive_a_slap_after_a_while);
    RUN_TEST(test_should_scream_in_the_air_and_comment_on_the_landing);
    RUN_TEST(test_should_sob_face_down_and_feel_betrayed_when_turned_back);
    RUN_TEST(test_should_faint_on_its_back);
    RUN_TEST(test_should_get_queasy_only_when_held_tilted);
    RUN_TEST(test_should_be_rattled_by_shaking_and_dizzy_after_a_hard_one);
    RUN_TEST(test_should_react_to_touches_and_bumps);
    RUN_TEST(test_should_let_a_slap_cut_through_anything_but_a_fall);
    RUN_TEST(test_should_not_get_bored_while_being_handled);
    RUN_TEST(test_should_hold_a_forced_reaction);
    RUN_TEST(test_should_hold_a_forced_reaction_while_being_handled);
    RUN_TEST(test_should_round_trip_reaction_names);
    return UNITY_END();
}
