#include <unity.h>

#include "wall.h"

using paint::Stage;
using paint::Touched;
using paint::Wall;

namespace {

constexpr int W = 320;
constexpr int H = 240;
constexpr float HOUR = 60 * 60;
constexpr float DRYING = 3 * HOUR;

// Lets time pass a minute at a time, as the board does a frame at a time.
void wait(Wall& wall, float seconds) {
    for (float t = 0.0f; t < seconds; t += 60.0f) wall.advance(seconds - t < 60.0f ? seconds - t : 60.0f);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_should_start_wet_on_the_first_coat() {
    Wall wall(W, H);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, wall.dryness());
    TEST_ASSERT_EQUAL(Stage::Wet, wall.stage());
    TEST_ASSERT_EQUAL_INT(1, wall.coat());
    TEST_ASSERT_FLOAT_WITHIN(1.0f, DRYING, wall.remainingS());
}

// Paint dries fastest at first: the first tenth of the time does more than the whole second half.
void test_should_dry_quickly_at_first_then_ever_more_slowly() {
    Wall wall(W, H);
    wait(wall, DRYING / 10);
    float firstTenth = wall.dryness();
    wait(wall, DRYING * 4 / 10);
    float halfway = wall.dryness();
    wait(wall, DRYING / 2);
    TEST_ASSERT_TRUE(firstTenth > 0.25f);
    TEST_ASSERT_TRUE(1.0f - halfway < firstTenth);
}

void test_should_be_dry_exactly_when_the_time_is_up_and_stay_dry() {
    Wall wall(W, H);
    wait(wall, DRYING - 60);
    TEST_ASSERT_TRUE(wall.dryness() < 1.0f);
    TEST_ASSERT_EQUAL(Stage::TouchDry, wall.stage());
    wait(wall, 60);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, wall.dryness());
    TEST_ASSERT_EQUAL(Stage::Dry, wall.stage());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, wall.remainingS());
    wait(wall, HOUR);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, wall.dryness());
}

void test_should_go_through_every_stage_in_order_without_ever_getting_wetter() {
    Wall wall(W, H);
    Stage last = wall.stage();
    float lastDryness = 0.0f;
    bool sawTacky = false;
    for (int minute = 0; minute <= 180; minute++) {
        TEST_ASSERT_TRUE(wall.dryness() >= lastDryness);
        TEST_ASSERT_TRUE(static_cast<int>(wall.stage()) >= static_cast<int>(last));
        sawTacky |= wall.stage() == Stage::Tacky;
        last = wall.stage();
        lastDryness = wall.dryness();
        wall.advance(60);
    }
    TEST_ASSERT_TRUE(sawTacky);
    TEST_ASSERT_EQUAL(Stage::Dry, last);
}

void test_should_keep_a_fingerprint_in_wet_paint() {
    Wall wall(W, H);
    TEST_ASSERT_EQUAL(Touched::Smudged, wall.touch(100, 80));
    TEST_ASSERT_EQUAL_UINT32(1, wall.smudges().size());
    TEST_ASSERT_EQUAL_INT(100, wall.smudges()[0].x);
    TEST_ASSERT_EQUAL_INT(80, wall.smudges()[0].y);
}

void test_should_shrug_off_a_finger_once_touch_dry() {
    Wall wall(W, H);
    wait(wall, DRYING * 0.8f);
    TEST_ASSERT_EQUAL(Stage::TouchDry, wall.stage());
    TEST_ASSERT_EQUAL(Touched::Spared, wall.touch(100, 80));
    TEST_ASSERT_EQUAL_UINT32(0, wall.smudges().size());
}

void test_should_start_another_coat_when_tapped_dry() {
    Wall wall(W, H);
    wall.touch(50, 50);
    wait(wall, DRYING);
    TEST_ASSERT_EQUAL(Touched::Recoated, wall.touch(100, 80));
    TEST_ASSERT_EQUAL_INT(2, wall.coat());
    TEST_ASSERT_EQUAL_FLOAT(0.0f, wall.dryness());
    TEST_ASSERT_EQUAL_UINT32(0, wall.smudges().size());  // painted over
}

void test_should_keep_only_the_latest_fingerprints() {
    Wall wall(W, H);
    for (int i = 0; i < 40; i++) wall.touch(i, i);
    TEST_ASSERT_EQUAL_UINT32(paint::MAX_SMUDGES, wall.smudges().size());
    TEST_ASSERT_EQUAL_INT(39, wall.smudges().back().x);
    TEST_ASSERT_EQUAL_INT(8, wall.smudges().front().x);
}

void test_should_let_drips_creep_while_wet_and_stop_once_tacky() {
    Wall wall(W, H);
    TEST_ASSERT_TRUE(wall.drips().size() >= 3);
    for (const auto& d : wall.drips()) TEST_ASSERT_EQUAL_FLOAT(0.0f, d.length);
    wait(wall, 10 * 60);
    float early = wall.drips()[0].length;
    TEST_ASSERT_TRUE(early > 0.0f);
    while (wall.stage() == Stage::Wet) wall.advance(60);
    float set = wall.drips()[0].length;
    wait(wall, HOUR);
    TEST_ASSERT_EQUAL_FLOAT(set, wall.drips()[0].length);
    for (const auto& d : wall.drips()) {
        TEST_ASSERT_TRUE(d.length <= d.most);
        TEST_ASSERT_TRUE(d.x >= 0.0f && d.x < W);
    }
}

void test_should_pick_up_a_saved_coat_where_it_left_off() {
    Wall original(W, H, 7);
    wait(original, DRYING);
    original.touch(5, 5);  // dry: a second coat
    wait(original, 40 * 60);
    Wall later(W, H, 7);
    later.restore(2, 40 * 60);
    TEST_ASSERT_EQUAL_INT(2, later.coat());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, original.dryness(), later.dryness());
    TEST_ASSERT_EQUAL_UINT32(original.drips().size(), later.drips().size());
    for (size_t i = 0; i < later.drips().size(); i++) {
        TEST_ASSERT_FLOAT_WITHIN(0.5f, original.drips()[i].x, later.drips()[i].x);
        TEST_ASSERT_FLOAT_WITHIN(0.5f, original.drips()[i].length, later.drips()[i].length);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_start_wet_on_the_first_coat);
    RUN_TEST(test_should_dry_quickly_at_first_then_ever_more_slowly);
    RUN_TEST(test_should_be_dry_exactly_when_the_time_is_up_and_stay_dry);
    RUN_TEST(test_should_go_through_every_stage_in_order_without_ever_getting_wetter);
    RUN_TEST(test_should_keep_a_fingerprint_in_wet_paint);
    RUN_TEST(test_should_shrug_off_a_finger_once_touch_dry);
    RUN_TEST(test_should_start_another_coat_when_tapped_dry);
    RUN_TEST(test_should_keep_only_the_latest_fingerprints);
    RUN_TEST(test_should_let_drips_creep_while_wet_and_stop_once_tacky);
    RUN_TEST(test_should_pick_up_a_saved_coat_where_it_left_off);
    return UNITY_END();
}
