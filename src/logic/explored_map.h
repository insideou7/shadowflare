#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sf {

// Which parts of an area the player has seen, for the map (N): one cell
// per 10 x 10 screen pixels of the map, as the original's mask
// (Save\M<scenario><slot>.msk, made at 0x417f30). Walking reveals a
// 68 x 46 block around the hero (0x41e4f0), most of the screen.
struct ExploredMap {
    int width = 0, height = 0;
    std::vector<std::uint8_t> seen;  // 1 = seen, top row first

    ExploredMap() = default;
    ExploredMap(int w, int h) : width(w), height(h), seen(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0) {}

    // Marks the block (x, y, w, h) as seen; the parts off the map are skipped.
    void reveal(int x, int y, int w, int h);
    bool seenAt(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height && seen[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
    }

    // As comma-separated numbers: width, height, then the lengths of the
    // runs of unseen and seen cells in turn, starting with unseen.
    std::string encode() const;
    // Throws FormatError on a malformed list.
    static ExploredMap decode(std::string_view text);
};

// The original's reveal: the block 680 x 460 screen pixels around the hero,
// in mask cells (0x41e4f0: ((x - 340) / 10, (y - 230) / 10, 68, 46)).
inline constexpr int kRevealLeft = 340, kRevealTop = 230, kRevealWidth = 68, kRevealHeight = 46;
inline constexpr int kMapScale = 10;  // screen pixels per map pixel

}  // namespace sf
