#include <unity.h>

#include "world.h"

using sand::Direction;
using sand::Material;
using sand::World;

namespace {

const Direction DOWN{0, 1};
const Direction RIGHT{1, 0};
const Direction DOWN_RIGHT{1, 1};
const Direction UP{0, -1};

void settle(World& w, Direction down, int steps = 200) {
    for (int i = 0; i < steps; i++) w.step(down);
}

// Drops grains one at a time at (x, 0), letting each land before the next.
void pour(World& w, int x, Material m, int grains) {
    for (int i = 0; i < grains; i++) {
        w.paint(x, 0, 0, m);
        settle(w, DOWN, w.height() + w.width());
    }
}

// Every grain is resting: with down being +y, none could still fall straight or slide diagonally.
bool allResting(const World& w) {
    for (int y = 0; y < w.height(); y++) {
        for (int x = 0; x < w.width(); x++) {
            if (w.at(x, y) != Material::Sand) continue;
            for (int dx : {0, -1, 1}) {
                Material below = w.at(x + dx, y + 1);
                if (below == Material::Empty || below == Material::Water) return false;
            }
        }
    }
    return true;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_should_start_empty_with_walls_all_around() {
    World w(10, 8);
    TEST_ASSERT_EQUAL_INT(0, w.count(Material::Sand) + w.count(Material::Water) + w.count(Material::Wall));
    TEST_ASSERT_EQUAL(Material::Empty, w.at(0, 0));
    TEST_ASSERT_EQUAL(Material::Wall, w.at(-1, 0));
    TEST_ASSERT_EQUAL(Material::Wall, w.at(10, 0));
    TEST_ASSERT_EQUAL(Material::Wall, w.at(0, 8));
}

void test_should_paint_a_disc() {
    World w(21, 21);
    w.paint(10, 10, 3, Material::Sand);
    TEST_ASSERT_EQUAL(Material::Sand, w.at(10, 10));
    TEST_ASSERT_EQUAL(Material::Sand, w.at(13, 10));
    TEST_ASSERT_EQUAL(Material::Empty, w.at(14, 10));
    TEST_ASSERT_EQUAL(Material::Empty, w.at(13, 13));  // outside the disc's rim
    TEST_ASSERT_EQUAL_INT(29, w.count(Material::Sand));
}

void test_should_sprinkle_only_part_of_a_disc() {
    World w(41, 41);
    w.paint(20, 20, 15, Material::Sand, 50);
    int painted = w.count(Material::Sand);
    TEST_ASSERT_TRUE(painted > 250 && painted < 450);  // about half of the ~700 cells
}

void test_should_let_sand_fall_to_the_floor() {
    World w(5, 10);
    w.paint(2, 0, 0, Material::Sand);
    uint8_t shade = w.shade(2, 0);
    settle(w, DOWN);
    TEST_ASSERT_EQUAL(Material::Sand, w.at(2, 9));
    TEST_ASSERT_EQUAL_UINT8(shade, w.shade(2, 9));  // the same grain, with its own colour
    TEST_ASSERT_EQUAL_INT(1, w.count(Material::Sand));
}

void test_should_pile_sand_into_a_heap_that_rests() {
    World w(21, 12);
    pour(w, 10, Material::Sand, 16);
    TEST_ASSERT_EQUAL_INT(16, w.count(Material::Sand));
    TEST_ASSERT_TRUE(allResting(w));
    int floor = 0;
    for (int x = 0; x < 21; x++) floor += w.at(x, 11) == Material::Sand;
    TEST_ASSERT_TRUE(floor >= 5);  // spread into a heap, not a tower
}

void test_should_let_water_spread_flat() {
    World w(8, 10);
    pour(w, 2, Material::Water, 8);
    settle(w, DOWN, 300);
    for (int x = 0; x < 8; x++) TEST_ASSERT_EQUAL(Material::Water, w.at(x, 9));
}

void test_should_sink_sand_through_water() {
    World w(6, 10);
    w.paintLine(0, 8, 5, 8, 0, Material::Water);
    w.paintLine(0, 9, 5, 9, 0, Material::Water);
    w.paintLine(0, 0, 5, 0, 0, Material::Sand);
    settle(w, DOWN, 300);
    for (int x = 0; x < 6; x++) {
        TEST_ASSERT_EQUAL(Material::Sand, w.at(x, 9));
        TEST_ASSERT_EQUAL(Material::Water, w.at(x, 7));
    }
}

void test_should_hold_sand_on_a_wall() {
    World w(10, 10);
    w.paintLine(0, 5, 9, 5, 0, Material::Wall);
    w.paint(4, 1, 1, Material::Sand);  // a plus sign of 5 grains
    settle(w, DOWN);
    for (int x = 0; x < 10; x++) TEST_ASSERT_EQUAL(Material::Wall, w.at(x, 5));
    for (int y = 6; y < 10; y++) {
        for (int x = 0; x < 10; x++) TEST_ASSERT_EQUAL(Material::Empty, w.at(x, y));
    }
    TEST_ASSERT_EQUAL_INT(5, w.count(Material::Sand));
}

void test_should_fall_whichever_way_down_is() {
    World w(10, 10);
    w.paint(2, 2, 0, Material::Sand);
    settle(w, RIGHT);
    TEST_ASSERT_EQUAL(Material::Sand, w.at(9, 2));
    settle(w, DOWN_RIGHT);
    TEST_ASSERT_EQUAL(Material::Sand, w.at(9, 9));
    settle(w, UP);
    TEST_ASSERT_EQUAL(Material::Sand, w.at(9, 0));
}

void test_should_keep_still_when_nothing_is_down() {
    World w(10, 10);
    w.paint(5, 2, 1, Material::Sand);
    settle(w, Direction{0, 0});
    TEST_ASSERT_EQUAL(Material::Sand, w.at(5, 2));
    TEST_ASSERT_EQUAL(Material::Sand, w.at(5, 1));
}

void test_should_let_walls_overwrite_and_erasing_clear() {
    World w(10, 10);
    w.paint(5, 5, 2, Material::Wall);
    w.paint(5, 5, 2, Material::Sand);  // sand does not go into walls
    TEST_ASSERT_EQUAL_INT(0, w.count(Material::Sand));
    w.paint(5, 5, 1, Material::Empty);
    TEST_ASSERT_EQUAL(Material::Empty, w.at(5, 5));
    w.paint(2, 2, 0, Material::Sand);
    w.paint(2, 2, 0, Material::Wall);
    TEST_ASSERT_EQUAL(Material::Wall, w.at(2, 2));
    w.clear();
    TEST_ASSERT_EQUAL_INT(0, w.count(Material::Wall));
}

void test_should_stir_grains_without_losing_any_or_moving_walls() {
    World w(30, 30);
    w.paintLine(0, 20, 29, 20, 0, Material::Wall);
    w.paint(15, 10, 6, Material::Sand, 50);
    w.paint(8, 10, 4, Material::Water, 50);
    int sandBefore = w.count(Material::Sand), waterBefore = w.count(Material::Water);
    World before = w;
    w.stir(100);
    int moved = 0;
    for (int y = 0; y < 30; y++) {
        for (int x = 0; x < 30; x++) moved += w.at(x, y) != before.at(x, y);
    }
    TEST_ASSERT_TRUE(moved > 20);
    TEST_ASSERT_EQUAL_INT(sandBefore, w.count(Material::Sand));
    TEST_ASSERT_EQUAL_INT(waterBefore, w.count(Material::Water));
    for (int x = 0; x < 30; x++) TEST_ASSERT_EQUAL(Material::Wall, w.at(x, 20));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_should_start_empty_with_walls_all_around);
    RUN_TEST(test_should_paint_a_disc);
    RUN_TEST(test_should_sprinkle_only_part_of_a_disc);
    RUN_TEST(test_should_let_sand_fall_to_the_floor);
    RUN_TEST(test_should_pile_sand_into_a_heap_that_rests);
    RUN_TEST(test_should_let_water_spread_flat);
    RUN_TEST(test_should_sink_sand_through_water);
    RUN_TEST(test_should_hold_sand_on_a_wall);
    RUN_TEST(test_should_fall_whichever_way_down_is);
    RUN_TEST(test_should_keep_still_when_nothing_is_down);
    RUN_TEST(test_should_let_walls_overwrite_and_erasing_clear);
    RUN_TEST(test_should_stir_grains_without_losing_any_or_moving_walls);
    return UNITY_END();
}
