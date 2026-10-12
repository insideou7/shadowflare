#include "formats/sprite_bank.h"

#include <algorithm>
#include <climits>
#include <string_view>

#include "formats/binary_reader.h"
#include "formats/rclib.h"

namespace sf {

namespace {

int rowStride(int bpp, int width) {
    int bytes = width;
    if (bpp == 1) bytes = (width + 7) / 8;
    if (bpp == 4) bytes = (width + 1) / 2;
    return (bytes + 3) / 4 * 4;
}

// Expands packed DIB rows (bottom-up, 4-byte aligned) to one index per pixel, top-down.
std::vector<std::uint8_t> expand(std::span<const std::uint8_t> src, int bpp, int w, int h) {
    const int stride = rowStride(bpp, w);
    if (src.size() < static_cast<std::size_t>(stride) * h) throw FormatError("sprite part too small");
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t* row = src.data() + static_cast<std::size_t>(h - 1 - y) * stride;
        std::uint8_t* dst = out.data() + static_cast<std::size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            switch (bpp) {
                case 1: dst[x] = (row[x / 8] >> (7 - x % 8)) & 1; break;
                case 4: dst[x] = (row[x / 2] >> (x % 2 ? 0 : 4)) & 0x0f; break;
                default: dst[x] = row[x]; break;
            }
        }
    }
    return out;
}

// Reads a count and checks the file has room for that many items of at
// least `minSize` bytes, so a corrupt count cannot trigger a huge allocation.
std::uint32_t count(BinaryReader& in, std::size_t minSize) {
    const std::uint32_t n = in.u32();
    if (n > in.remaining() / minSize) throw FormatError("bad count in sprite bank");
    return n;
}

}  // namespace

SpriteBank SpriteBank::parse(std::span<const std::uint8_t> data) {
    BinaryReader in(data);
    const std::string_view magic = in.chars(16);
    SpriteBank bank;
    if (magic.starts_with("NJudgeUniPat")) {
        bank.kind = SpriteBankKind::Plain;
    } else if (magic.starts_with("UnitePatData")) {
        bank.kind = SpriteBankKind::WithJudge;
    } else if (magic.starts_with("ShadowLowPat")) {
        bank.kind = SpriteBankKind::Shadow;
    } else {
        throw FormatError("not a sprite bank");
    }
    bank.version = (magic[12] - '0') * 100 + (magic[13] - '0') * 10 + (magic[14] - '0');
    if (bank.version < 0 || bank.version > 3) throw FormatError("unsupported sprite bank version");

    const std::uint32_t partCount = count(in, 16);
    if (bank.version >= 3) in.u32();  // total pixel bytes, only used for allocation
    bank.parts.reserve(partCount);
    for (std::uint32_t i = 0; i < partCount; ++i) {
        SpritePart part;
        part.bpp = static_cast<int>(in.u32());
        part.width = in.i32();
        part.height = in.i32();
        const bool compressed = in.u32() != 0;
        if (bank.kind == SpriteBankKind::Shadow) part.bpp = 1;
        if (part.width < 0 || part.height < 0 || part.width > 4096 || part.height > 4096) {
            throw FormatError("bad sprite part size");
        }
        const std::size_t size = static_cast<std::size_t>(rowStride(part.bpp, part.width)) * part.height;
        if (compressed) {
            auto raw = rclib::readBlock(in);
            part.pixels = expand(raw, part.bpp, part.width, part.height);
        } else if (size > 0) {
            part.pixels = expand(in.bytes(size), part.bpp, part.width, part.height);
        }
        bank.parts.push_back(std::move(part));
    }

    const std::uint32_t patternCount = count(in, 20);
    if (bank.version >= 3) in.u32();  // total part list entries
    bank.patterns.reserve(patternCount);
    for (std::uint32_t i = 0; i < patternCount; ++i) {
        Pattern pattern;
        const std::uint32_t listCount = count(in, 28);
        pattern.x = in.i32();
        pattern.y = in.i32();
        pattern.width = in.i32();
        pattern.height = in.i32();
        if (pattern.width < 0 || pattern.height < 0 || pattern.width > 4096 || pattern.height > 4096) {
            throw FormatError("bad pattern size");
        }
        if (bank.kind == SpriteBankKind::WithJudge) in.skip(0xa8);  // collision data, not decoded yet
        if (bank.version >= 1) pattern.palette = in.i32();
        pattern.parts.reserve(listCount);
        for (std::uint32_t k = 0; k < listCount; ++k) {
            PatternPart p;
            p.flags = in.i32();
            p.part = in.i32();
            p.x = in.i32();
            p.y = in.i32();
            p.unknown = in.i32();
            p.scaleX = in.i32();
            p.scaleY = in.i32();
            pattern.parts.push_back(p);
        }
        bank.patterns.push_back(std::move(pattern));
    }

    const std::uint32_t paletteCount = count(in, 1024);
    bank.palettes.resize(paletteCount);
    for (auto& palette : bank.palettes) {
        for (auto& c : palette) {
            c.r = in.u8();
            c.g = in.u8();
            c.b = in.u8();
            in.u8();
        }
    }

    // Version 2+ may append a name for every pattern.
    if (bank.version >= 2) {
        while (bank.names.size() < patternCount && in.remaining() >= 4) {
            const std::uint32_t len = in.u32();
            bank.names.emplace_back(in.chars(len));
        }
    }
    return bank;
}

bool SpriteBank::bounds(const Pattern& pattern, int& x, int& y, int& w, int& h) const {
    int x0 = INT_MAX, y0 = INT_MAX, x1 = INT_MIN, y1 = INT_MIN;
    for (const auto& p : pattern.parts) {
        if (p.part < 0 || static_cast<std::size_t>(p.part) >= parts.size()) continue;
        const auto& part = parts[p.part];
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x + part.width);
        y1 = std::max(y1, p.y + part.height);
    }
    if (x0 >= x1 || y0 >= y1) return false;
    if (pattern.width > 0 && pattern.height > 0) {
        x = pattern.x;
        y = pattern.y;
        w = pattern.width;
        h = pattern.height;
    } else {
        x = x0;
        y = y0;
        w = x1 - x0;
        h = y1 - y0;
    }
    return true;
}

SpriteBank::Picture SpriteBank::render(int pattern, bool opaque) const {
    Picture picture;
    if (pattern < 0 || static_cast<std::size_t>(pattern) >= patterns.size()) return picture;
    const Pattern& pat = patterns[pattern];
    int x0, y0, w, h;
    if (!bounds(pat, x0, y0, w, h)) return picture;
    // Some shadow banks carry no palette; any non-zero index is then drawn black.
    static const Palette kFallback{};
    const Palette& palette = palettes.empty() ? kFallback
                             : pat.palette >= 0 && static_cast<std::size_t>(pat.palette) < palettes.size()
                                 ? palettes[pat.palette]
                                 : palettes[0];
    picture = {x0, y0, w, h, std::vector<std::uint32_t>(static_cast<std::size_t>(w) * h, 0)};
    for (const auto& placed : pat.parts) {
        if (placed.part < 0 || static_cast<std::size_t>(placed.part) >= parts.size()) continue;
        const SpritePart& part = parts[placed.part];
        for (int y = 0; y < part.height; ++y) {
            const int ty = placed.y - y0 + y;
            if (ty < 0 || ty >= h) continue;
            for (int x = 0; x < part.width; ++x) {
                const int tx = placed.x - x0 + x;
                if (tx < 0 || tx >= w) continue;
                if (part.bpp == 32) {
                    const std::uint32_t c = part.argb[static_cast<std::size_t>(y) * part.width + x];
                    if (c >> 24) picture.argb[static_cast<std::size_t>(ty) * w + tx] = c;
                    continue;
                }
                const std::uint8_t index = part.pixels[static_cast<std::size_t>(y) * part.width + x];
                if (index == 0 && !opaque) continue;
                const Rgb c = palette[index];
                picture.argb[static_cast<std::size_t>(ty) * w + tx] =
                    0xff000000u | (static_cast<std::uint32_t>(c.r) << 16) | (c.g << 8) | c.b;
            }
        }
    }
    return picture;
}

}  // namespace sf
