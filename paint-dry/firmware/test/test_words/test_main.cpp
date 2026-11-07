#include <string.h>
#include <unity.h>

#include "words.h"

using paint::Stage;
using paint::Touched;

namespace {

char text[32];

const char* remaining(float seconds) {
    paint::formatRemaining(seconds, text, sizeof(text));
    return text;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_should_say_how_long_is_left_in_hours_and_minutes() {
    TEST_ASSERT_EQUAL_STRING("3 h 0 min", remaining(3 * 3600));
    TEST_ASSERT_EQUAL_STRING("2 h 38 min", remaining(2 * 3600 + 38 * 60 + 20));
    TEST_ASSERT_EQUAL_STRING("45 min", remaining(45 * 60));
    TEST_ASSERT_EQUAL_STRING("1 min", remaining(61));
    TEST_ASSERT_EQUAL_STRING("under a minute", remaining(30));
    TEST_ASSERT_EQUAL_STRING("dry", remaining(0));
}

void test_should_not_overflow_a_short_buffer() {
    char small[6];
    paint::formatRemaining(2 * 3600 + 38 * 60, small, sizeof(small));
    TEST_ASSERT_EQUAL_UINT32(5, strlen(small));
}

void test_should_name_every_stage() {
    TEST_ASSERT_EQUAL_STRING("wet", paint::stageName(Stage::Wet));
    TEST_ASSERT_EQUAL_STRING("tacky", paint::stageName(Stage::Tacky));
    TEST_ASSERT_EQUAL_STRING("touch-dry", paint::stageName(Stage::TouchDry));
    TEST_ASSERT_EQUAL_STRING("dry", paint::stageName(Stage::Dry));
}

void test_should_keep_a_remark_for_a_few_minutes_then_move_on() {
    const char* first = paint::remark(Stage::Wet, 60);
    TEST_ASSERT_EQUAL_STRING(first, paint::remark(Stage::Wet, 120));
    bool changed = false;
    for (float t = 600; t < 3600; t += 60) changed |= strcmp(first, paint::remark(Stage::Wet, t)) != 0;
    TEST_ASSERT_TRUE(changed);
}

void test_should_have_its_own_remarks_for_each_stage() {
    TEST_ASSERT_TRUE(strcmp(paint::remark(Stage::Wet, 0), paint::remark(Stage::Dry, 0)) != 0);
    TEST_ASSERT_TRUE(strcmp(paint::remark(Stage::Tacky, 0), paint::remark(Stage::TouchDry, 0)) != 0);
}

void test_should_answer_every_kind_of_touch() {
    TEST_ASSERT_TRUE(strlen(paint::touchReply(Touched::Smudged)) > 0);
    TEST_ASSERT_TRUE(strcmp(paint::touchReply(Touched::Smudged), paint::touchReply(Touched::Spared)) != 0);
    TEST_ASSERT_TRUE(strcmp(paint::touchReply(Touched::Spared), paint::touchReply(Touched::Recoated)) != 0);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_say_how_long_is_left_in_hours_and_minutes);
    RUN_TEST(test_should_not_overflow_a_short_buffer);
    RUN_TEST(test_should_name_every_stage);
    RUN_TEST(test_should_keep_a_remark_for_a_few_minutes_then_move_on);
    RUN_TEST(test_should_have_its_own_remarks_for_each_stage);
    RUN_TEST(test_should_answer_every_kind_of_touch);
    return UNITY_END();
}
