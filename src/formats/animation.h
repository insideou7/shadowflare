#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace sf {

// .caf character animation charts. From RKC_RPGSCRN_CHARANIM::ReadCafFile.
//
// An animation set holds one chart per action (idle, walk, attack, ...).
// Each chart has 9 direction slots (8 used: 0 = south, then counter-clockwise
// on screen through east, north and west). A direction has several layers, drawn
// together each frame: the shadow, the body and, for the player, one
// layer per equipment slot.
struct AnimationSet {
    struct Cell {
        std::int16_t status = 0;  // 8 = shadow (pattern is in the .sdw bank)
        std::int16_t trans = 1000;
        std::int32_t pattern = -1;
        std::int16_t priority = 0;  // draw order within the frame, low first
    };
    using Layer = std::vector<Cell>;  // one cell per frame
    struct Direction {
        std::int16_t frameCount = 0;
        std::vector<Layer> layers;
    };
    struct Chart {
        std::int16_t status = 0;  // 1 on looping actions such as idle and walk
        std::vector<Direction> directions;
    };

    static constexpr int kDirections = 9;
    static constexpr std::int16_t kShadowStatus = 8;

    int version = 0;
    std::vector<Chart> charts;

    static AnimationSet parse(std::span<const std::uint8_t> data);
};

}  // namespace sf
