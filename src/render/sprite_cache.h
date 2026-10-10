#pragma once

#include <SDL.h>

#include <cstdint>
#include <unordered_map>

namespace sf {

struct SpriteBank;

// A pattern rendered to a texture, with the offset of its top-left
// corner from the anchor point it is drawn at.
struct SpriteTexture {
    SDL_Texture* texture = nullptr;
    int originX = 0;
    int originY = 0;
    int width = 0;
    int height = 0;
};

// Converts sprite bank patterns into SDL textures on first use.
class SpriteCache {
public:
    explicit SpriteCache(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~SpriteCache();
    SpriteCache(const SpriteCache&) = delete;
    SpriteCache& operator=(const SpriteCache&) = delete;

    // `opaque` keeps palette index 0 instead of treating it as transparent (ground chips).
    const SpriteTexture* get(const SpriteBank& bank, int pattern, bool opaque);
    void clear();

private:
    SDL_Renderer* renderer_;
    std::unordered_map<std::uint64_t, SpriteTexture> textures_;
};

}  // namespace sf
