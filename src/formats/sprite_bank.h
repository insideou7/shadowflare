#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sf {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
};
using Palette = std::array<Rgb, 256>;

// One bitmap inside a sprite bank. Pixels are palette indices,
// one byte per pixel, top row first (the files store rows bottom-up).
struct SpritePart {
    int bpp = 8;  // 1, 4 or 8 in the file; 32 for open content (see argb)
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
    // Full-colour pixels with alpha, 0xAARRGGBB, used instead of `pixels`
    // and the palette when bpp is 32.
    std::vector<std::uint32_t> argb;
};

// A placed part within a pattern. Field names follow the original
// engine's RKC_UPDIB_PARTSLIST where known.
struct PatternPart {
    std::int32_t flags = 0;
    std::int32_t part = -1;  // index into SpriteBank::parts, out of range = none
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t unknown = 0;
    std::int32_t scaleX = 1000;  // per mille
    std::int32_t scaleY = 1000;
};

// A drawable frame: one or more parts composed at offsets.
struct Pattern {
    std::int32_t x = 0, y = 0, width = 0, height = 0;  // bounds; zero when unset
    std::int32_t palette = 0;
    std::vector<PatternPart> parts;
};

enum class SpriteBankKind { Plain, WithJudge, Shadow };

// .njp ("NJudgeUniPat"/"UnitePatData") and .sdw ("ShadowLowPat") files.
// Layout reverse engineered from RKC_UPDIB_UPD::Read (RKC_UPDIB.dll).
struct SpriteBank {
    SpriteBankKind kind = SpriteBankKind::Plain;
    int version = 0;
    std::vector<SpritePart> parts;
    std::vector<Pattern> patterns;
    std::vector<Palette> palettes;
    std::vector<std::string> names;

    static SpriteBank parse(std::span<const std::uint8_t> data);

    // Bounds of a pattern, computed from its parts when the file leaves them unset.
    // Returns false for patterns with nothing to draw.
    bool bounds(const Pattern& pattern, int& x, int& y, int& w, int& h) const;
};

}  // namespace sf
