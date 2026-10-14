#include "render/sprite_cache.h"

#include <algorithm>
#include <cstdlib>

namespace sf {

SpriteCache::~SpriteCache() { clear(); }

void SpriteCache::clear() {
    for (auto* cache : {&textures_, &outlines_}) {
        for (auto& [key, sprite] : *cache) {
            if (sprite.texture) SDL_DestroyTexture(sprite.texture);
        }
        cache->clear();
    }
}

SpriteBank::Picture outlineOf(const SpriteBank::Picture& picture) {
    constexpr int k = kOutlineWidth;
    SpriteBank::Picture ring;
    if (picture.argb.empty()) return ring;
    ring.x = picture.x - k;
    ring.y = picture.y - k;
    ring.width = picture.width + 2 * k;
    ring.height = picture.height + 2 * k;
    ring.argb.assign(static_cast<std::size_t>(ring.width) * ring.height, 0);
    auto solid = [&](int x, int y) {
        x -= k;
        y -= k;
        if (x < 0 || y < 0 || x >= picture.width || y >= picture.height) return false;
        return (picture.argb[static_cast<std::size_t>(y) * picture.width + x] >> 24) >= 0x80;
    };
    for (int y = 0; y < ring.height; ++y) {
        for (int x = 0; x < ring.width; ++x) {
            if (solid(x, y)) continue;
            // How near the shape is, by the largest step to it.
            int near = 0;
            for (int dy = -k; dy <= k && near < k; ++dy) {
                for (int dx = -k; dx <= k; ++dx) {
                    if (solid(x + dx, y + dy)) near = std::max(near, k + 1 - std::max(std::abs(dx), std::abs(dy)));
                }
            }
            if (near == 0) continue;
            const std::uint32_t alpha = 255u * static_cast<std::uint32_t>(near) / k;
            ring.argb[static_cast<std::size_t>(y) * ring.width + x] = (alpha << 24) | 0xffffffu;
        }
    }
    return ring;
}

SDL_Texture* makeTexture(SDL_Renderer* renderer, const SpriteBank::Picture& picture) {
    if (picture.argb.empty()) return nullptr;
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, picture.width, picture.height);
    if (!texture) return nullptr;
    SDL_UpdateTexture(texture, nullptr, picture.argb.data(), picture.width * 4);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    return texture;
}

const SpriteTexture* SpriteCache::outline(const SpriteBank& bank, int pattern) {
    if (pattern < 0 || static_cast<std::size_t>(pattern) >= bank.patterns.size()) return nullptr;
    const std::uint64_t key = (reinterpret_cast<std::uintptr_t>(&bank) << 20) ^ static_cast<std::uint64_t>(pattern);
    if (auto it = outlines_.find(key); it != outlines_.end()) return it->second.texture ? &it->second : nullptr;
    const auto ring = outlineOf(bank.render(pattern, false));
    SpriteTexture sprite{makeTexture(renderer_, ring), ring.x, ring.y, ring.width, ring.height};
    auto& stored = outlines_[key] = sprite;
    return stored.texture ? &stored : nullptr;
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
