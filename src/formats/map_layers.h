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

    // Open content only: the cells are isometric tiles in world space, each
    // isoTile world units square and drawn with its sprite's anchor at the
    // tile's centre. 0 (the original) = chips on a screen-aligned grid.
    int isoTile = 0;

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

    // The area the ground covers on screen, in map pixels.
    void screenBounds(int& left, int& top, int& right, int& bottom) const;

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
    // Footprint: left, top, right, bottom in world units from x, y. x, y is
    // where the sprite's top left corner lands, so for a tall sprite (a wall
    // or a tree) this box, near the sprite's bottom, is where it stands.
    std::int32_t judge[4] = {};

    // Draw order key, smaller first: the footprint's back corner, as
    // RKC_RPGSCRN_OBJECTDISP::InsertSort uses.
    long long depth() const { return static_cast<long long>(x) + judge[0] + y + judge[1]; }

    // True when something standing at a world position is behind this
    // object and must be drawn before it. Compares against the footprint
    // point nearest that position, so a long wall hides what is behind any
    // part of it, not just behind one corner.
    bool hides(double worldX, double worldY) const;
};

struct ObjectLayer {
    std::vector<MapObject> objects;
    static ObjectLayer parse(std::span<const std::uint8_t> data);
};

// Screen pixels per 100 world units along each axis: RKC_RPGSCRN's "base
// parameters". The original game uses 15/10 (a 3:2 diamond); an open content
// pack can choose its own, e.g. 20/10 for 2:1 isometric art. Set once at
// startup, before any map is loaded.
struct Projection {
    int x = 15;
    int y = 10;
};
inline Projection projection;

// World to screen projection used by RKC_RPGSCRN::CalcRealPos.
inline void worldToScreen(int wx, int wy, int& sx, int& sy) {
    sx = (wx - wy) * projection.x / 100;
    sy = (wx + wy) * projection.y / 100;
}

// Inverse of worldToScreen (RKC_RPGSCRN::CalcWorldPos).
inline void screenToWorld(double sx, double sy, double& wx, double& wy) {
    const double a = sx * 100 / projection.x;  // wx - wy
    const double b = sy * 100 / projection.y;  // wx + wy
    wx = (a + b) / 2;
    wy = (b - a) / 2;
}

}  // namespace sf
