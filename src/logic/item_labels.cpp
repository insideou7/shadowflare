#include "logic/item_labels.h"

namespace sf {

void stackLabels(std::vector<LabelBox>& boxes, int gap) {
    for (std::size_t i = 1; i < boxes.size(); ++i) {
        LabelBox& box = boxes[i];
        // Each move is upward, past a box already placed, so this ends
        // after at most i moves.
        for (bool moved = true; moved;) {
            moved = false;
            for (std::size_t j = 0; j < i; ++j) {
                if (box.overlaps(boxes[j])) {
                    box.y = boxes[j].y - box.h - gap;
                    moved = true;
                }
            }
        }
    }
}

}  // namespace sf
