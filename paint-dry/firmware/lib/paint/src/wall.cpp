#include "wall.h"

#include <math.h>

#include <algorithm>

namespace paint {

namespace {

constexpr float CURVE = 3.0f;         // how front-loaded drying is: about 82% dry at the halfway mark
constexpr float WET_UNTIL = 0.4f;     // drying up to here is wet (and drips creep), ~30 min...
constexpr float TACKY_UNTIL = 0.75f;  // ...up to here tacky, ~75 min; after that touch-dry until the end
constexpr uint32_t MIN_DRIPS = 4;
constexpr uint32_t EXTRA_DRIPS = 4;
constexpr int DRIP_MARGIN_PX = 12;
constexpr uint32_t SHORTEST_DRIP_PX = 18;
constexpr uint32_t LONGEST_DRIP_PX = 90;

}  // namespace

Wall::Wall(int width, int height, uint32_t seed, float dryingS)
    : _width(width), _height(height), _seed(seed != 0 ? seed : 1), _dryingS(dryingS) {
    startCoat(1);
}

// xorshift32
uint32_t Wall::random() {
    _state ^= _state << 13;
    _state ^= _state >> 17;
    _state ^= _state << 5;
    return _state;
}

// Each coat drips its own way, and the same way every time, so a restored coat looks as it did.
void Wall::startCoat(int coat) {
    _coat = coat;
    _elapsedS = 0.0f;
    _smudges.clear();
    _state = _seed * 2654435761u + static_cast<uint32_t>(coat) * 40503u;
    if (_state == 0) _state = 1;
    uint32_t count = MIN_DRIPS + random() % (EXTRA_DRIPS + 1);
    _drips.clear();
    for (uint32_t i = 0; i < count; i++) {
        Drip d;
        d.x = static_cast<float>(DRIP_MARGIN_PX + random() % static_cast<uint32_t>(_width - 2 * DRIP_MARGIN_PX));
        d.most = static_cast<float>(SHORTEST_DRIP_PX + random() % (LONGEST_DRIP_PX - SHORTEST_DRIP_PX));
        _drips.push_back(d);
    }
}

void Wall::restore(int coat, float elapsedS) {
    startCoat(std::max(coat, 1));
    advance(elapsedS);
}

// Drips run quickly at first and slow as the paint thickens, coming to rest the moment it turns tacky.
void Wall::advance(float seconds) {
    _elapsedS = std::min(_elapsedS + seconds, _dryingS);
    float wet = std::min(dryness() / WET_UNTIL, 1.0f);
    float run = 1.0f - (1.0f - wet) * (1.0f - wet);
    for (Drip& d : _drips) d.length = d.most * run;
}

float Wall::dryness() const {
    if (_elapsedS >= _dryingS) return 1.0f;
    return (1.0f - expf(-CURVE * _elapsedS / _dryingS)) / (1.0f - expf(-CURVE));
}

Stage Wall::stage() const {
    if (_elapsedS >= _dryingS) return Stage::Dry;
    float d = dryness();
    if (d < WET_UNTIL) return Stage::Wet;
    return d < TACKY_UNTIL ? Stage::Tacky : Stage::TouchDry;
}

float Wall::remainingS() const {
    return std::max(_dryingS - _elapsedS, 0.0f);
}

Touched Wall::touch(int x, int y) {
    switch (stage()) {
        case Stage::Wet:
        case Stage::Tacky:
            break;
        case Stage::TouchDry:
            return Touched::Spared;
        case Stage::Dry:
            startCoat(_coat + 1);
            return Touched::Recoated;
    }
    Smudge s;
    s.x = static_cast<int16_t>(std::min(std::max(x, 0), _width - 1));
    s.y = static_cast<int16_t>(std::min(std::max(y, 0), _height - 1));
    s.turn = static_cast<uint8_t>(random() & 0xFF);
    _smudges.push_back(s);
    if (_smudges.size() > MAX_SMUDGES) _smudges.erase(_smudges.begin());
    return Touched::Smudged;
}

}  // namespace paint
