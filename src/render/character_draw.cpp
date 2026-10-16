#include "render/character_draw.h"

#include <algorithm>
#include <vector>

#include "formats/map_layers.h"
#include "render/sprite_cache.h"
#include "world/character.h"

namespace sf {

void drawCharacter(SDL_Renderer* renderer, SpriteCache& sprites, const Character& character, int cameraX, int cameraY,
                   int shadowAlpha, int alpha) {
    const auto* dir = character.currentDirection();
    const CharacterModel* model = character.model();
    const int frame = character.frame();
    if (!dir) return;
    int sx, sy;
    worldToScreen(static_cast<int>(character.x), static_cast<int>(character.y), sx, sy);
    sx -= cameraX;
    sy -= cameraY;

    struct Item {
        const AnimationSet::Cell* cell;
        bool shadow;
        int layer;
    };
    std::vector<Item> items;
    for (std::size_t i = 0; i < dir->layers.size(); ++i) {
        const auto& layer = dir->layers[i];
        if (frame >= static_cast<int>(layer.size())) continue;
        const auto& cell = layer[frame];
        const bool shadow = AnimationSet::isShadow(cell.status);
        if (shadow && alpha < 255) continue;
        if (!shadow && std::ranges::find(character.visibleLayers, static_cast<int>(i)) == character.visibleLayers.end()) continue;
        items.push_back({&cell, shadow, static_cast<int>(i)});
    }
    // Shadows first, then the rest from the highest priority down: a
    // lower number is nearer the viewer (armour over the body).
    std::ranges::stable_sort(items, [](const Item& a, const Item& b) {
        if (a.shadow != b.shadow) return a.shadow;
        return a.cell->priority > b.cell->priority;
    });

    for (const auto& item : items) {
        const SpriteBank& bank = item.shadow ? model->shadow : model->body;
        const SpriteTexture* sprite = sprites.get(bank, item.cell->pattern, false, character.grey && !item.shadow);
        if (!sprite) continue;
        const bool additive = !item.shadow && (item.cell->status & AnimationSet::kAdditiveStatus);
        const int trans = std::clamp(static_cast<int>(item.cell->trans), 0, 1000);
        // Textures are shared: set their colour for this draw and put it
        // back after.
        if (item.shadow) {
            SDL_SetTextureColorMod(sprite->texture, 0, 0, 0);
            SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(shadowAlpha * character.tint.a / 255));
        } else if (additive) {
            // Glows and streaks add light at their own opacity, untinted
            // (the partner's coat colour is set on its body layer only).
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(trans * 255 / 1000 * alpha / 255 * character.tint.a / 255));
        } else {
            SDL_Color c{character.tint.r, character.tint.g, character.tint.b, character.tint.a};
            for (const auto& t : character.layerTints) {
                if (t.layer != item.layer) continue;
                // SDL can only darken: strengths over 1000 stay at full.
                auto scale = [](Uint8 v, int permille) { return static_cast<Uint8>(v * std::clamp(permille, 0, 1000) / 1000); };
                c = {scale(c.r, t.r), scale(c.g, t.g), scale(c.b, t.b), c.a};
            }
            SDL_SetTextureColorMod(sprite->texture, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(trans * 255 / 1000 * alpha / 255 * character.tint.a / 255));
        }
        SDL_Rect dst{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
        SDL_RenderCopy(renderer, sprite->texture, nullptr, &dst);
        if (additive) SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
        if (!item.shadow && !additive && character.glow > 0) {
            // A second copy added on top brightens it.
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(std::min(1000, character.glow) * 255 / 1000 * alpha / 255 * character.tint.a / 255));
            SDL_RenderCopy(renderer, sprite->texture, nullptr, &dst);
            SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
        }
        if (!item.shadow) {
            SDL_SetTextureColorMod(sprite->texture, 255, 255, 255);
            SDL_SetTextureAlphaMod(sprite->texture, 255);
        }
    }
}

SDL_Rect characterBounds(SpriteCache& sprites, const Character& character) {
    SDL_Rect bounds{0, 0, 0, 0};
    const auto* dir = character.currentDirection();
    const CharacterModel* model = character.model();
    const int frame = character.frame();
    if (!dir) return bounds;
    int sx, sy;
    worldToScreen(static_cast<int>(character.x), static_cast<int>(character.y), sx, sy);
    for (std::size_t i = 0; i < dir->layers.size(); ++i) {
        const auto& layer = dir->layers[i];
        if (frame >= static_cast<int>(layer.size())) continue;
        const auto& cell = layer[frame];
        if (cell.status == AnimationSet::kShadowStatus) continue;
        if (std::ranges::find(character.visibleLayers, static_cast<int>(i)) == character.visibleLayers.end()) continue;
        const SpriteTexture* sprite = sprites.get(model->body, cell.pattern, false, character.grey);
        if (!sprite) continue;
        const SDL_Rect r{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
        if (SDL_RectEmpty(&bounds)) {
            bounds = r;
        } else {
            SDL_UnionRect(&bounds, &r, &bounds);
        }
    }
    return bounds;
}

}  // namespace sf
