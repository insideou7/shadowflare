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

    // Walkability grid ("area judge"). It is laid out in world space, one
    // cell per magX x magY world units, so cell (jx, jy) covers world
    // x in [jx*magX, (jx+1)*magX). judgeX/judgeY is the cell stored first.
    // Bit 0 set = blocked (walls, trees, outside the map). Bit 15 is set
    // near some map edges but its meaning is unknown; exits are script
    // triggers (see ScenarioScript).
    int judgeX = 0, judgeY = 0;
    int judgeWidth = 0, judgeHeight = 0;
    std::vector<std::int16_t> judge;  // row-major

    static constexpr std::int16_t kBlocked = 0x0001;

    const Cell& at(int x, int y) const { return cells[static_cast<std::size_t>(y) * width + x]; }

    // Judge value of the cell containing a world position; blocked off the grid.
    std::int16_t judgeAt(double worldX, double worldY) const;
    bool walkable(double worldX, double worldY) const { return !(judgeAt(worldX, worldY) & kBlocked); }

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

// Inverse of worldToScreen (RKC_RPGSCRN::CalcWorldPos).
inline void screenToWorld(double sx, double sy, double& wx, double& wy) {
    const double a = sx * 100 / 15;  // wx - wy
    const double b = sy * 100 / 10;  // wx + wy
    wx = (a + b) / 2;
    wy = (b - a) / 2;
}

}  // namespace sf
