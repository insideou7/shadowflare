#pragma once

#include <vector>

namespace sf {

// A name box over an item on the ground, in screen pixels.
struct LabelBox {
    int x = 0, y = 0, w = 0, h = 0;
    bool overlaps(const LabelBox& o) const { return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h; }
};

// Moves the boxes up until none covers another, as Diablo stacks the names
// of a pile of loot (ours; the original names only the item under the
// pointer). The first box keeps its place, and each later one rises just
// above whichever placed box it would cover, so the items nearest the front
// should come first. `gap` pixels are left between stacked boxes.
void stackLabels(std::vector<LabelBox>& boxes, int gap = 1);

}  // namespace sf
