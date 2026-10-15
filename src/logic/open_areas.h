#pragma once

namespace sf {

struct SaveGame;

// The open content pack's first and second areas (62 maps) is laid on the original's
// (tools/content/first_area.py): the same scenario numbers plus
// kOpenScenarioOffset, each map a crop of the original's, walked on cell for
// cell. A point in the original's world is (x0, y0) further on than in ours.
// With it a saved game moves between the two.
inline constexpr int kOpenScenarioOffset = 10000000;

struct OpenArea {
    int original = 0;        // the original's scenario number
    int x0 = 0, y0 = 0;      // the original world point at our map's (0, 0)
    int width = 0, height = 0;                  // our map, in world units
    int originalWidth = 0, originalHeight = 0;  // the original's ground, in screen pixels
};

// The area laid on the original's scenario, or null for the ones the open
// pack does not have.
const OpenArea* openAreaOf(int original);

// A save made with the original's data, moved to stand in the open pack's
// map of the same place, or one made with the open pack moved to the
// original's: the hero's area and place, and what the area map has seen of
// each area both have. False, with the save unchanged, when the hero's area
// is not one of those.
bool moveSaveToOtherData(SaveGame& save);

}  // namespace sf
