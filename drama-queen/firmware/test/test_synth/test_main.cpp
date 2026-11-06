#include <stdlib.h>
#include <string.h>
#include <unity.h>

#include "synth.h"

using drama::Synth;
using drama::VoiceStyle;

namespace {

constexpr uint32_t RATE = 16000;
constexpr size_t CAPACITY = RATE * 4;
int16_t buffer[CAPACITY];

int peak(const Synth& s) {
    int p = 0;
    for (size_t i = 0; i < s.length(); i++) p = abs(s.data()[i]) > p ? abs(s.data()[i]) : p;
    return p;
}

}  // namespace

void setUp() {
    memset(buffer, 0, sizeof(buffer));
}

void tearDown() {}

void test_should_make_a_sweep_exactly_as_long_as_asked() {
    Synth s(buffer, CAPACITY, RATE);
    s.sweep(500.0f, 1800.0f, 250, 0.5f);
    TEST_ASSERT_EQUAL_UINT32(RATE / 4, s.length());
    TEST_ASSERT_INT_WITHIN(1200, static_cast<int>(0.5f * 32767), peak(s));
}

void test_should_add_silence_after_sound() {
    Synth s(buffer, CAPACITY, RATE);
    s.sweep(400.0f, 400.0f, 100, 0.5f);
    s.silence(100);
    TEST_ASSERT_EQUAL_UINT32(RATE / 5, s.length());
    TEST_ASSERT_EQUAL_INT16(0, s.data()[s.length() - 1]);
}

void test_should_babble_longer_for_longer_lines() {
    VoiceStyle style;
    Synth shortLine(buffer, CAPACITY, RATE);
    shortLine.babble("Eek!", style, 7);
    size_t shortLength = shortLine.length();
    Synth longLine(buffer, CAPACITY, RATE);
    longLine.babble("I'm literally the main character.", style, 7);
    TEST_ASSERT_TRUE(longLine.length() > shortLength * 3);
    TEST_ASSERT_TRUE(peak(longLine) > 1000);
}

void test_should_sound_the_same_for_the_same_seed() {
    VoiceStyle style;
    static int16_t other[CAPACITY];
    Synth a(buffer, CAPACITY, RATE);
    Synth b(other, CAPACITY, RATE);
    a.babble("How dare you!", style, 42);
    b.babble("How dare you!", style, 42);
    TEST_ASSERT_EQUAL_UINT32(a.length(), b.length());
    TEST_ASSERT_EQUAL_INT16_ARRAY(a.data(), b.data(), a.length());
}

void test_should_never_write_past_the_buffer() {
    static int16_t small[1000 + 16];
    for (int i = 1000; i < 1016; i++) small[i] = 1234;  // guard values after the end
    Synth s(small, 1000, RATE);
    s.sweep(300.0f, 900.0f, 2000, 0.8f);
    s.babble("This line is far too long to fit in a tiny buffer at all", VoiceStyle(), 1);
    TEST_ASSERT_EQUAL_UINT32(1000, s.length());
    for (int i = 1000; i < 1016; i++) TEST_ASSERT_EQUAL_INT16(1234, small[i]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_make_a_sweep_exactly_as_long_as_asked);
    RUN_TEST(test_should_add_silence_after_sound);
    RUN_TEST(test_should_babble_longer_for_longer_lines);
    RUN_TEST(test_should_sound_the_same_for_the_same_seed);
    RUN_TEST(test_should_never_write_past_the_buffer);
    return UNITY_END();
}
