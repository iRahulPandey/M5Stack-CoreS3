#pragma once

#include <stdint.h>

#include <vector>

// On the ESP32 the step's inner loop runs from internal RAM: run from flash it shares the instruction
// cache with the screen-pushing code on the other core, which keeps evicting it.
#if defined(ESP_PLATFORM)
#include <esp_attr.h>
#define SAND_HOT IRAM_ATTR
#define SAND_TABLE DRAM_ATTR
#else
#define SAND_HOT
#define SAND_TABLE
#endif

namespace sand {

enum class Material : uint8_t { Empty = 0, Sand = 1, Water = 2, Wall = 3 };

// Which way is down, as one of the eight neighbours: dx, dy each -1, 0 or 1 (y grows down the screen).
// 0, 0 means nothing falls.
struct Direction {
    int8_t dx = 0;
    int8_t dy = 1;
};

// A grid of grains. Each step every grain tries to fall one cell "down", whichever way down is:
// sand slides off diagonally when blocked and sinks through water, water also spreads sideways, walls
// never move. Each grain keeps its own shade wherever it goes. Outside the grid counts as wall.
class World {
public:
    World(int width, int height, uint32_t seed = 1);

    int width() const { return _width; }
    int height() const { return _height; }
    Material at(int x, int y) const;
    uint8_t shade(int x, int y) const;  // 0..31, picked when the grain was painted

    // Fills a disc of cells. Sand and water only go into empty cells, and only `density` percent of
    // them (a sprinkle looks like grains); walls overwrite anything; Empty erases.
    void paint(int cx, int cy, int radius, Material material, int density = 100);
    void paintLine(int x0, int y0, int x1, int y1, int radius, Material material, int density = 100);
    void clear();

    void step(Direction down);
    void stir(int percent);  // a shake: that share of the loose grains jump to a random nearby empty cell
    int count(Material material) const;

    // The raw cells, row by row, for drawing 17,000 of them fast; decode with materialOf and shadeOf.
    const uint8_t* cells() const { return _cells.data(); }
    static Material materialOf(uint8_t cell) { return static_cast<Material>(cell & MATERIAL_MASK); }
    static uint8_t shadeOf(uint8_t cell) { return cell >> SHADE_SHIFT; }

private:
    static constexpr uint8_t MATERIAL_MASK = 0x03;
    static constexpr uint8_t MOVED = 0x04;
    static constexpr int SHADE_SHIFT = 3;

    uint8_t& cell(int x, int y) { return _cells[y * _width + x]; }
    uint8_t cell(int x, int y) const { return _cells[y * _width + x]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < _width && y < _height; }
    uint32_t random();
    void put(int x, int y, Material material);
    bool canEnter(int x, int y, int direction, bool sinks) const;
    void move(int x, int y, int direction);
    bool tryMove(int x, int y, int direction, bool sinks);
    bool tryEither(int x, int y, int first, int second, bool sinks);
    void fall(int x, int y, int down);

    int _width;
    int _height;
    std::vector<uint8_t> _cells;  // material in bits 0-1, "moved this step" in bit 2, shade in bits 3-7
    uint32_t _seed;
    bool _flip = false;  // alternates the sweep across rows, so nothing drifts to one side
};

}  // namespace sand
