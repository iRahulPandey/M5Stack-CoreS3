#include "world.h"

#include <stdlib.h>

#include <algorithm>
#include <utility>

namespace sand {

namespace {

constexpr uint32_t SHADES = 32;
constexpr uint32_t STIR_REACH = 3;  // a stirred grain jumps up to this many cells

// The eight neighbours, clockwise from the right (y grows down): one turn is 45 degrees.
SAND_TABLE const int8_t DX[8] = {1, 1, 0, -1, -1, -1, 0, 1};
SAND_TABLE const int8_t DY[8] = {0, 1, 1, 1, 0, -1, -1, -1};

int directionIndex(Direction d) {
    for (int i = 0; i < 8; i++) {
        if (DX[i] == d.dx && DY[i] == d.dy) return i;
    }
    return -1;
}

int turn(int direction, int by) {
    return (direction + by + 8) % 8;
}

bool isLoose(Material m) {
    return m == Material::Sand || m == Material::Water;
}

}  // namespace

World::World(int width, int height, uint32_t seed)
    : _width(width), _height(height), _cells(static_cast<size_t>(width) * height, 0), _seed(seed != 0 ? seed : 1) {}

Material World::at(int x, int y) const {
    return inside(x, y) ? materialOf(cell(x, y)) : Material::Wall;
}

uint8_t World::shade(int x, int y) const {
    return inside(x, y) ? cell(x, y) >> SHADE_SHIFT : 0;
}

// xorshift32: cheap, and unlike a plain LCG its low bits do not repeat in short cycles, which matters
// for the coin flips that pick a grain's way round an obstacle.
SAND_HOT uint32_t World::random() {
    _seed ^= _seed << 13;
    _seed ^= _seed >> 17;
    _seed ^= _seed << 5;
    return _seed;
}

void World::put(int x, int y, Material material) {
    uint8_t shade = static_cast<uint8_t>(random() % SHADES);
    cell(x, y) = material == Material::Empty ? 0 : static_cast<uint8_t>(shade << SHADE_SHIFT | static_cast<uint8_t>(material));
}

void World::paint(int cx, int cy, int radius, Material material, int density) {
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            int dx = x - cx, dy = y - cy;
            if (!inside(x, y) || dx * dx + dy * dy > radius * radius) continue;
            if (isLoose(material) && (at(x, y) != Material::Empty || static_cast<int>(random() % 100) >= density)) {
                continue;
            }
            put(x, y, material);
        }
    }
}

void World::paintLine(int x0, int y0, int x1, int y1, int radius, Material material, int density) {
    int n = std::max(abs(x1 - x0), abs(y1 - y0));
    for (int i = 0; i <= n; i++) {
        int x = n == 0 ? x0 : x0 + (x1 - x0) * i / n;
        int y = n == 0 ? y0 : y0 + (y1 - y0) * i / n;
        paint(x, y, radius, material, density);
    }
}

void World::clear() {
    std::fill(_cells.begin(), _cells.end(), 0);
}

// Sweeps from the far side of "down", so a falling column moves as one instead of a grain a step.
SAND_HOT void World::step(Direction down) {
    int d = directionIndex(down);
    if (d < 0) return;
    for (uint8_t& c : _cells) c &= static_cast<uint8_t>(~MOVED);
    _flip = !_flip;
    bool yBackwards = down.dy > 0 || (down.dy == 0 && _flip);
    bool xBackwards = down.dx > 0 || (down.dx == 0 && _flip);
    for (int i = 0; i < _height; i++) {
        int y = yBackwards ? _height - 1 - i : i;
        const uint8_t* row = &_cells[y * _width];
        for (int j = 0; j < _width; j++) {
            int x = xBackwards ? _width - 1 - j : j;
            if (row[x] != 0) fall(x, y, d);  // most cells are empty: skip them without a call
        }
    }
}

SAND_HOT void World::fall(int x, int y, int down) {
    uint8_t c = cell(x, y);
    if (c & MOVED) return;
    Material m = materialOf(c);
    if (m == Material::Sand) {
        if (!tryMove(x, y, down, true)) tryEither(x, y, turn(down, 1), turn(down, -1), true);
    } else if (m == Material::Water) {
        if (tryMove(x, y, down, false) || tryEither(x, y, turn(down, 1), turn(down, -1), false)) return;
        tryEither(x, y, turn(down, 2), turn(down, -2), false);
    }
}

// Most grains in a heap are blocked both ways, so look before flipping a coin.
SAND_HOT bool World::tryEither(int x, int y, int first, int second, bool sinks) {
    bool canFirst = canEnter(x, y, first, sinks), canSecond = canEnter(x, y, second, sinks);
    if (!canFirst && !canSecond) return false;
    if (canFirst && canSecond && (random() & 1)) std::swap(first, second);
    move(x, y, canFirst ? first : second);
    return true;
}

SAND_HOT bool World::tryMove(int x, int y, int direction, bool sinks) {
    if (!canEnter(x, y, direction, sinks)) return false;
    move(x, y, direction);
    return true;
}

// The cell that way is empty, or holds water and the grain `sinks` through it.
SAND_HOT bool World::canEnter(int x, int y, int direction, bool sinks) const {
    int nx = x + DX[direction], ny = y + DY[direction];
    if (!inside(nx, ny)) return false;
    Material there = materialOf(cell(nx, ny));
    return there == Material::Empty || (sinks && there == Material::Water);
}

// Swaps the grain at x, y with whatever is that way (nothing, or water); both count as moved this step.
SAND_HOT void World::move(int x, int y, int direction) {
    int nx = x + DX[direction], ny = y + DY[direction];
    uint8_t there = cell(nx, ny);
    cell(nx, ny) = cell(x, y) | MOVED;
    cell(x, y) = there == 0 ? 0 : static_cast<uint8_t>(there | MOVED);
}

void World::stir(int percent) {
    for (int y = 0; y < _height; y++) {
        for (int x = 0; x < _width; x++) {
            if (!isLoose(materialOf(cell(x, y))) || static_cast<int>(random() % 100) >= percent) continue;
            int direction = static_cast<int>(random() % 8);
            int reach = static_cast<int>(1 + random() % STIR_REACH);
            int nx = x + DX[direction] * reach, ny = y + DY[direction] * reach;
            if (!inside(nx, ny) || materialOf(cell(nx, ny)) != Material::Empty) continue;
            cell(nx, ny) = cell(x, y);
            cell(x, y) = 0;
        }
    }
}

int World::count(Material material) const {
    int n = 0;
    for (uint8_t c : _cells) n += c != 0 && materialOf(c) == material;
    return n;
}

}  // namespace sand
