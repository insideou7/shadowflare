// Increased-power mode (see PowerMode in logic/magic.h): P or the power
// button on the panel starts it once the hero has killed enough monsters,
// and a blue glow plays around the hero while it runs.
#include <algorithm>

#include "core/log.h"
#include "game/game.h"

namespace sf {

namespace {

// The button lights up over the panel's crossed swords at the left
// (Bar.njp pattern 12, drawn by 0x4039f0), and a click between (25, 408)
// and (58, 430) presses it (0x445bd0).
constexpr int kReadyPattern = 12;
constexpr SDL_Rect kButton{25, 408, 34, 23};

bool inside(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

}  // namespace

bool Game::startPower() {
    if (!player_ || !power_.start(hasSpiritStone())) return false;
    // It turns Berserker off (+0x15dc).
    setSpellOn(kBerserker, false);
    powerAuraTicks_ = 0;
    log::info("Increased-power mode on");
    return true;
}

bool Game::powerButtonClick(int x, int y) {
    if (!player_ || menuOpen_ || !inside(kButton, x, y)) return false;
    // Only a click while it blinks does anything; otherwise the panel has it.
    // The button clicks; the P key is silent (0x445bd0).
    if (!startPower()) return false;
    playSound(0x3a);
    return true;
}

// While the mode is ready the button flickers: lit at full brightness for
// two of the original's frames, then at 800 for two (0x4039f0).
void Game::renderPowerButton() {
    if (!player_ || menuOpen_ || !power_.ready(hasSpiritStone()) || !hasPanelArt()) return;
    const SpriteBank* bar = itemBank("System/Game/Pattern/Bar");
    const SpriteTexture* t = bar ? sprites_.get(*bar, kReadyPattern, false) : nullptr;
    if (!t) return;
    const bool bright = (ticks_ * 3 / 2) % 4 < 2;  // +0xa7ec & 3 < 2
    const Uint8 shade = bright ? 255 : 204;
    SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(t->texture, shade, shade, shade);
    const SDL_Rect dst{t->originX, t->originY, t->width, t->height};
    SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
    SDL_SetTextureColorMod(t->texture, 255, 255, 255);
    SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_BLEND);
}

// Player/Common/Powerup, the original's animation block 500: one chart of
// 30 frames with no direction, added to what is behind, played a frame a
// tick at the hero's feet while the mode runs (0x4448b0).
const CharacterModel* Game::powerModel() { return model("Player/Common/Powerup"); }

void Game::renderPowerAura(int camX, int camY) {
    if (!player_ || !power_.active()) return;
    renderHeroOverlay(powerModel(), powerAuraTicks_, camX, camY);
}

void Game::renderHeroOverlay(const CharacterModel* m, double ticks, int camX, int camY) {
    if (!player_ || !m || m->animations.charts.empty()) return;
    const auto& chart = m->animations.charts[0];
    const AnimationSet::Direction* dir = nullptr;
    for (int d : {8, 0}) {
        if (static_cast<std::size_t>(d) < chart.directions.size() && chart.directions[static_cast<std::size_t>(d)].frameCount > 0) {
            dir = &chart.directions[static_cast<std::size_t>(d)];
            break;
        }
    }
    if (!dir) return;
    const int frame = static_cast<int>(ticks) % dir->frameCount;
    int sx, sy;
    worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), sx, sy);
    for (const auto& layer : dir->layers) {
        if (frame >= static_cast<int>(layer.size())) continue;
        const auto& cell = layer[static_cast<std::size_t>(frame)];
        if (cell.pattern < 0 || AnimationSet::isShadow(cell.status)) continue;
        const SpriteTexture* t = sprites_.get(m->body, cell.pattern, false);
        if (!t) continue;
        SDL_SetTextureBlendMode(t->texture, cell.status & AnimationSet::kAdditiveStatus ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(t->texture, static_cast<Uint8>(std::clamp(static_cast<int>(cell.trans), 0, 1000) * 255 / 1000));
        const SDL_Rect dst{sx - camX + t->originX, sy - camY + t->originY, t->width, t->height};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        SDL_SetTextureAlphaMod(t->texture, 255);
    }
}

}  // namespace sf
