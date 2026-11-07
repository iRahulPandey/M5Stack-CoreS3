#pragma once

#include <stdint.h>

#include <vector>

namespace paint {

constexpr float DEFAULT_DRYING_S = 3 * 60 * 60;  // three hours, in real time
constexpr size_t MAX_SMUDGES = 32;

enum class Stage : uint8_t { Wet, Tacky, TouchDry, Dry };

// What touching the wall did.
enum class Touched : uint8_t { Smudged, Spared, Recoated };

struct Drip {
    float x = 0.0f;
    float length = 0.0f;  // pixels below the top edge
    float most = 0.0f;    // how far it will get before the paint sets
};

struct Smudge {
    int16_t x = 0, y = 0;
    uint8_t turn = 0;  // which way the fingerprint's ridges lie, 0..255 for a half turn
};

// One coat of paint drying on a wall, in real time: quickly at first, then ever more slowly, as paint
// does. While it is wet, drips creep down from the top and a finger leaves a print that stays until the
// next coat; touch-dry paint shrugs a finger off; dry paint takes a tap as the cue for another coat.
class Wall {
public:
    Wall(int width, int height, uint32_t seed = 1, float dryingS = DEFAULT_DRYING_S);

    void advance(float seconds);
    void restore(int coat, float elapsedS);  // pick up where a saved coat left off

    float dryness() const;  // 0 just painted, 1 dry
    Stage stage() const;
    float elapsedS() const { return _elapsedS; }
    float remainingS() const;
    int coat() const { return _coat; }  // 1 for the first coat

    Touched touch(int x, int y);
    const std::vector<Drip>& drips() const { return _drips; }
    const std::vector<Smudge>& smudges() const { return _smudges; }

private:
    void startCoat(int coat);
    uint32_t random();

    int _width;
    int _height;
    uint32_t _seed;
    uint32_t _state = 1;
    float _dryingS;
    float _elapsedS = 0.0f;
    int _coat = 1;
    std::vector<Drip> _drips;
    std::vector<Smudge> _smudges;
};

}  // namespace paint
