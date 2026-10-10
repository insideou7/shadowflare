#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace sf {

// .gnd: the ground tile grid. From RKC_RPGSCRN_GROUNDBLOCK::ReadFile.
struct GroundLayer {
    struct Cell {
        std::int16_t flags = 0;
        std::int16_t bank = -1;     // index into the map's .lst; -1 = empty
        std::int16_t pattern = -1;  // pattern within that bank
    };

    int width = 0;   // in chips
    int height = 0;
    int chipWidth = 64;   // pixels
    int chipHeight = 48;
    int magX = 160, magY = 160;
    std::vector<Cell> cells;  // row-major

    const Cell& at(int x, int y) const { return cells[static_cast<std::size_t>(y) * width + x]; }

    static GroundLayer parse(std::span<const std::uint8_t> data);
};

// .obl: props, walls and scenery placed in world coordinates.
// From RKC_RPGSCRN_OBJECTBLOCK::ReadFile.
struct MapObject {
    std::int32_t x = 0, y = 0;  // world units
    std::int16_t bank = -1;     // index into the map's .lst
    std::int16_t pattern = -1;
    std::int16_t status = 0;
    std::int16_t trans = 1000;
    std::int16_t height = 0;
    std::int16_t palette = 0;
    std::int16_t r = 1000, g = 1000, b = 1000;  // per mille tint
    std::int32_t judge[4] = {};                  // collision box
};

struct ObjectLayer {
    std::vector<MapObject> objects;
    static ObjectLayer parse(std::span<const std::uint8_t> data);
};

// World to screen projection used by RKC_RPGSCRN::CalcRealPos
// (default base parameters 15/10).
inline void worldToScreen(int wx, int wy, int& sx, int& sy) {
    sx = (wx - wy) * 15 / 100;
    sy = (wx + wy) * 10 / 100;
}

}  // namespace sf
