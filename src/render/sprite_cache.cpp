#include "render/sprite_cache.h"

#include <vector>

#include "formats/sprite_bank.h"

namespace sf {

SpriteCache::~SpriteCache() { clear(); }

void SpriteCache::clear() {
    for (auto& [key, sprite] : textures_) {
        if (sprite.texture) SDL_DestroyTexture(sprite.texture);
    }
    textures_.clear();
}

const SpriteTexture* SpriteCache::get(const SpriteBank& bank, int pattern, bool opaque) {
    if (pattern < 0 || static_cast<std::size_t>(pattern) >= bank.patterns.size()) return nullptr;
    const std::uint64_t key = (reinterpret_cast<std::uintptr_t>(&bank) << 20) ^ (static_cast<std::uint64_t>(pattern) << 1) ^
                              static_cast<std::uint64_t>(opaque);
    if (auto it = textures_.find(key); it != textures_.end()) {
        return it->second.texture ? &it->second : nullptr;
    }

    SpriteTexture sprite;
    const Pattern& pat = bank.patterns[pattern];
    int x0, y0, w, h;
    if (bank.bounds(pat, x0, y0, w, h)) {
        // Some shadow banks carry no palette; any non-zero index is then drawn black.
        static const Palette kFallback{};
        const Palette& palette =
            bank.palettes.empty() ? kFallback
            : pat.palette >= 0 && static_cast<std::size_t>(pat.palette) < bank.palettes.size()
                ? bank.palettes[pat.palette]
                : bank.palettes[0];
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w) * h, 0);
        for (const auto& placed : pat.parts) {
            if (placed.part < 0 || static_cast<std::size_t>(placed.part) >= bank.parts.size()) continue;
            const SpritePart& part = bank.parts[placed.part];
            for (int y = 0; y < part.height; ++y) {
                const int ty = placed.y - y0 + y;
                if (ty < 0 || ty >= h) continue;
                for (int x = 0; x < part.width; ++x) {
                    const int tx = placed.x - x0 + x;
                    if (tx < 0 || tx >= w) continue;
                    if (part.bpp == 32) {
                        const std::uint32_t c = part.argb[static_cast<std::size_t>(y) * part.width + x];
                        if (c >> 24) pixels[static_cast<std::size_t>(ty) * w + tx] = c;
                        continue;
                    }
                    const std::uint8_t index = part.pixels[static_cast<std::size_t>(y) * part.width + x];
                    if (index == 0 && !opaque) continue;
                    const Rgb c = palette[index];
                    pixels[static_cast<std::size_t>(ty) * w + tx] =
                        0xff000000u | (static_cast<std::uint32_t>(c.r) << 16) | (c.g << 8) | c.b;
                }
            }
        }
        sprite.texture =
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (sprite.texture) {
            SDL_UpdateTexture(sprite.texture, nullptr, pixels.data(), w * 4);
            SDL_SetTextureBlendMode(sprite.texture, SDL_BLENDMODE_BLEND);
            sprite.originX = x0;
            sprite.originY = y0;
            sprite.width = w;
            sprite.height = h;
        }
    }
    auto& stored = textures_[key] = sprite;
    return stored.texture ? &stored : nullptr;
}

}  // namespace sf
