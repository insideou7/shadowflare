#pragma once

#include <cstdint>
#include <vector>

#include "formats/sprite_bank.h"

namespace sf {

// Drawing the original game's ground with an open content pack's ground
// textures (see Skin in open_content.h). The original paints its ground on
// pre-drawn chips; each pixel is matched to the nearest of the materials
// its bank is painted with, by colour, and our texture for that material
// is drawn there instead. So paths, shores and clearings keep the
// original's shapes, in our art.

// A seamless texture, repeating every width x height pixels.
struct MaterialTexture {
    int width = 0, height = 0;
    std::vector<std::uint32_t> argb;  // 0xAARRGGBB, top row first
    std::uint32_t at(int x, int y) const {
        x %= width;
        y %= height;
        if (x < 0) x += width;
        if (y < 0) y += height;
        return argb[static_cast<std::size_t>(y) * width + x];
    }
};

// Lays out `period` x `period` isometric tiles of a sheet (frames first,
// first + 1, ... row by row, anchored at their centres) on their diamond
// lattice. The tiles repeat every `period` tiles, so the result is seamless;
// it is 2 * halfWidth * period by 2 * halfHeight * period pixels, where the
// half sizes are half a tile's diamond on screen. Empty if a frame is missing.
MaterialTexture buildMaterialTexture(const SpriteBank& tiles, int first, int period, int halfWidth, int halfHeight);

// The texture scaled, colour by colour, so that its average is `target`
// (within reason: each channel is scaled by 0.6 to 1.6), so our ground is
// as light or dark as the original's in each place.
MaterialTexture tinted(const MaterialTexture& texture, Rgb target);

// The materials one original ground bank is painted with.
struct GroundRule {
    struct Entry {
        const MaterialTexture* texture = nullptr;  // null = black
        Rgb colour;                                // its typical colour in the original
    };
    std::vector<Entry> entries;
};

// Paints a w x h area of the ground whose top-left is map pixel (x0, y0).
// `original` and `rule` cover the area plus `margin` pixels all round
// (row-major, (w + 2 * margin) wide): the original's colours and the index
// in `rules` of the bank each pixel comes from (-1 = no rule: the original
// colour is kept). Colours are smoothed over `margin` pixels before they
// are matched, so speckles in the original do not show through.
std::vector<std::uint32_t> paintGround(int x0, int y0, int w, int h, int margin, const std::vector<std::uint32_t>& original,
                                       const std::vector<std::int16_t>& rule, const std::vector<GroundRule>& rules);

}  // namespace sf
