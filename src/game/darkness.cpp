// Darkness in dungeons, ruins, towers and caves (Display Darkness in the
// Settings Menu), as ShadowFlare.exe draws it in its frame code (0x41e4f0).
//
// Each frame it copies Dark.njp's part 0, a 640 x 420 ellipse of light
// brightest at the middle of the screen, adds Dark.njp's part 3 round each
// player and part (light + 1) round each character whose Scenario.Mct
// record carries a light (torches, braziers, glowing monsters), saturating
// at 255, gives the result a grey palette from the area's light level and
// multiplies the top 420 rows of the screen by it. Areas with light 2000
// (towns and fields) are skipped, as is everything with the option off.
#include <algorithm>

#include "game/game.h"

namespace sf {

bool Game::darknessShown() const {
    return settings_.darkness && world_.map && player_ && world_.control.light != ScenarioControl::kLit;
}

// Dark.njp's parts from the player's copy, or our own soft ellipses of the
// same sizes for the open content pack.
const LightMap* Game::darkPart(int part) {
    if (part < 0 || part >= static_cast<int>(darkParts_.size())) return nullptr;
    auto& slot = darkParts_[static_cast<std::size_t>(part)];
    if (!slot) {
        const SpriteBank* bank = itemBank("System/Game/Pattern/Dark");
        if (bank && part < static_cast<int>(bank->parts.size()) && bank->parts[static_cast<std::size_t>(part)].bpp == 8) {
            const SpritePart& p = bank->parts[static_cast<std::size_t>(part)];
            slot = LightMap{p.width, p.height, p.pixels};
        } else {
            const auto [w, h] = lighting::kPartSizes[static_cast<std::size_t>(part)];
            slot = lighting::ellipse(w, h);
        }
    }
    return &*slot;
}

void Game::renderDarkness(int camX, int camY) {
    const LightMap* screenLight = darkPart(0);
    if (!screenLight || screenLight->width != lighting::kWidth || screenLight->height != lighting::kHeight) return;
    // Zoomed out the world, darkness and all, is drawn bigger and shrunk,
    // so the light round the hero covers as much of the map as at full
    // size, and the screen's ellipse keeps its place about the middle of
    // the view (at full size it covers 640 x 420, the panel below).
    const int w = viewWidth(), h = viewHeight() - (kScreenHeight - lighting::kHeight);
    LightMap light;
    if (w == lighting::kWidth && h == lighting::kHeight) {
        light = *screenLight;
    } else {
        light = LightMap{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h))};
        lighting::add(light, *screenLight, w / 2, h / 2);
    }
    auto addAt = [&](int part, double worldX, double worldY) {
        const LightMap* l = darkPart(part);
        if (!l) return;
        int sx, sy;
        worldToScreen(static_cast<int>(worldX), static_cast<int>(worldY), sx, sy);
        lighting::add(light, *l, sx - camX, sy - camY);
    };
    // The players (only the hero offline) each get part 3.
    addAt(lighting::kPlayerLight + 1, player_->x, player_->y);
    // Characters that carry a light. Monsters lose theirs when they die;
    // map objects keep their place in the scenario.
    for (const auto& actor : world_.actors) {
        if (actor.placement.light < 0 || !actor.active || (actor.hostile() && !actor.alive())) continue;
        addAt(actor.placement.light + 1, actor.character.x, actor.character.y);
    }
    for (const auto& p : world_.control.placements) {
        if (p.light < 0 || (p.group != ScenarioControl::Group::Object && p.group != ScenarioControl::Group::Item)) continue;
        addAt(p.light + 1, p.x, p.y);
    }

    const auto levels = lighting::levels(world_.control.light);
    if (!darkTexture_ || darkTextureW_ != w || darkTextureH_ != h) {
        darkTexture_.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h));
        if (!darkTexture_) return;
        SDL_SetTextureBlendMode(darkTexture_.get(), SDL_BLENDMODE_MOD);
        darkTextureW_ = w;
        darkTextureH_ = h;
    }
    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(darkTexture_.get(), nullptr, &pixels, &pitch) != 0) return;
    for (int y = 0; y < light.height; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(pixels) + static_cast<std::ptrdiff_t>(y) * pitch);
        for (int x = 0; x < light.width; ++x) {
            const std::uint32_t v = levels[light.at(x, y)];
            row[x] = 0xff000000u | v << 16 | v << 8 | v;
        }
    }
    SDL_UnlockTexture(darkTexture_.get());
    const SDL_Rect dst{0, 0, w, h};
    SDL_RenderCopy(renderer_, darkTexture_.get(), nullptr, &dst);
}

}  // namespace sf
