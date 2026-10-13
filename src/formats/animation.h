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
        std::int16_t status = 0;  // 8 = shadow (pattern is in the .sdw bank), 16 = additive
        std::int16_t trans = 1000;  // opacity per mille
        std::int32_t pattern = -1;
        std::int16_t priority = 0;  // draw order within the frame, high first (low is in front)
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
    // Shadow cells carry other flags too (0x40 where a blow lands, sound
    // cues 0x400-0x1000), so only the bit says it is one.
    static constexpr bool isShadow(int status) { return (status & kShadowStatus) != 0; }
    static constexpr std::int16_t kAdditiveStatus = 16;  // effect glows (OPTION animations)
    static constexpr std::int16_t kStrikeStatus = 0x40;  // the frame a blow lands on

    int version = 0;
    std::vector<Chart> charts;

    static AnimationSet parse(std::span<const std::uint8_t> data);
};

}  // namespace sf
