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

const SpriteTexture* SpriteCache::get(const SpriteBank& bank, int pattern, bool opaque) {
    if (pattern < 0 || static_cast<std::size_t>(pattern) >= bank.patterns.size()) return nullptr;
    const std::uint64_t key = (reinterpret_cast<std::uintptr_t>(&bank) << 20) ^ (static_cast<std::uint64_t>(pattern) << 1) ^
                              static_cast<std::uint64_t>(opaque);
    if (auto it = textures_.find(key); it != textures_.end()) {
        return it->second.texture ? &it->second : nullptr;
    }

    SpriteTexture sprite;
    const auto picture = bank.render(pattern, opaque);
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
