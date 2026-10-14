#pragma once

#include <SDL.h>

#include <cstdint>
#include <unordered_map>

#include "formats/sprite_bank.h"

namespace sf {

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
    // `grey` drops the colour (a monster turned to stone).
    const SpriteTexture* get(const SpriteBank& bank, int pattern, bool opaque, bool grey = false);
    // A white ring around the pattern's shape, kOutlineWidth pixels wide,
    // for the outline of what is under the pointer. Tint it with the
    // texture's colour mod.
    const SpriteTexture* outline(const SpriteBank& bank, int pattern);
    void clear();

private:
    SDL_Renderer* renderer_;
    std::unordered_map<std::uint64_t, SpriteTexture> textures_;
    std::unordered_map<std::uint64_t, SpriteTexture> outlines_;
};

inline constexpr int kOutlineWidth = 2;
// The ring around a picture's shape: white, solid next to it and fading
// outwards; the picture grows by kOutlineWidth on every side.
SpriteBank::Picture outlineOf(const SpriteBank::Picture& picture);
// A texture of a picture, or null when it is empty.
SDL_Texture* makeTexture(SDL_Renderer* renderer, const SpriteBank::Picture& picture);

}  // namespace sf
