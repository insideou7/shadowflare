#include "render/sprite_cache.h"

#include "formats/sprite_bank.h"

namespace sf {

SpriteCache::~SpriteCache() { clear(); }

void SpriteCache::clear() {
    for (auto& [key, sprite] : textures_) {
        if (sprite.texture) SDL_DestroyTexture(sprite.texture);
    }
    textures_.clear();
}

const SpriteTexture* SpriteCache::get(const SpriteBank& bank, int pattern, bool opaque, bool grey) {
    if (pattern < 0 || static_cast<std::size_t>(pattern) >= bank.patterns.size()) return nullptr;
    const std::uint64_t key = (reinterpret_cast<std::uintptr_t>(&bank) << 20) ^ (static_cast<std::uint64_t>(pattern) << 2) ^
                              (static_cast<std::uint64_t>(grey) << 1) ^ static_cast<std::uint64_t>(opaque);
    if (auto it = textures_.find(key); it != textures_.end()) {
        return it->second.texture ? &it->second : nullptr;
    }

    SpriteTexture sprite;
    auto picture = bank.render(pattern, opaque);
    if (grey) {
        for (auto& p : picture.argb) {
            const std::uint32_t r = (p >> 16) & 0xff, g = (p >> 8) & 0xff, b = p & 0xff;
            const std::uint32_t l = (r * 77 + g * 150 + b * 29) >> 8;
            p = (p & 0xff000000u) | (l << 16) | (l << 8) | l;
        }
    }
    if (!picture.argb.empty()) {
        const int w = picture.width, h = picture.height;
        sprite.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
        if (sprite.texture) {
            SDL_UpdateTexture(sprite.texture, nullptr, picture.argb.data(), w * 4);
            SDL_SetTextureBlendMode(sprite.texture, SDL_BLENDMODE_BLEND);
            sprite.originX = picture.x;
            sprite.originY = picture.y;
            sprite.width = w;
            sprite.height = h;
        }
    }
    auto& stored = textures_[key] = sprite;
    return stored.texture ? &stored : nullptr;
}

}  // namespace sf
