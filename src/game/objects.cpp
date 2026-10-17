// Scenario objects: the chests, doors, levers, switches and gates a
// scenario places, as ShadowFlare.exe's object class (vtable 0x476de8)
// keeps and draws them.

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "game/game.h"
#include "render/character_draw.h"
#include "game/skin.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

constexpr double kObjectReach = 320.0;  // as near as the hero must stand to work one (as kTalkRange)

bool inside(const ScenarioControl::Placement& p, double x, double y) {
    return x >= p.x + p.rect[0] && x <= p.x + p.rect[2] && y >= p.y + p.rect[1] && y <= p.y + p.rect[3];
}

Uint8 perMille(int value) { return static_cast<Uint8>(std::clamp(value, 0, 1000) * 255 / 1000); }

}  // namespace

void Game::spawnObjects() {
    world_.sceneObjects.clear();
    world_.unlockSwitch = false;
    for (std::size_t i = 0; i < world_.control.placements.size(); ++i) {
        const auto& p = world_.control.placements[i];
        if (p.group != ScenarioControl::Group::Object) continue;
        SceneObject o;
        o.placement = i;
        if (!p.look.prop.empty()) {
            // An open pack's object: a named frame of its props sheet.
            if (const SpriteBank* props = itemBank("Map/props")) {
                const auto found = std::ranges::find(props->names, p.look.prop);
                if (found != props->names.end()) {
                    o.body = props;
                    o.prop = static_cast<int>(found - props->names.begin());
                } else {
                    log::warn("{}: no prop called {} in Map/props.sheet", world_.scenarioDir, p.look.prop);
                }
            }
        } else if (p.kind >= 0 && p.look.pattern >= 0) {
            const std::string stem = std::format("Character/OBJECT/{:08d}/Pattern", p.kind);
            o.body = itemBank(stem);
            auto [it, inserted] = objectShadows_.try_emplace(stem);
            if (inserted) {
                try {
                    if (auto sdw = data_->read(stem + ".sdw")) it->second = std::make_unique<SpriteBank>(SpriteBank::parse(*sdw));
                } catch (const FormatError& e) {
                    log::warn("{}: {}", stem, e.what());
                }
            }
            o.shadow = it->second.get();
        } else if (p.kind >= 0 && p.look.chart >= 0) {
            if (const CharacterModel* m = model(std::format("Character/OBJECT/{:08d}/Animation", p.kind))) {
                o.animation.emplace(m);
                o.animation->x = p.x;
                o.animation->y = p.y;
                // Objects face no way: the chart's ninth direction, as
                // 0x45ddd0 asks for (the first when a chart has only one).
                const auto& charts = m->animations.charts;
                const bool ninth = static_cast<std::size_t>(p.look.chart) < charts.size() && charts[p.look.chart].directions.size() > 8;
                o.animation->direction = ninth ? 8 : 0;
                o.animation->restart(p.look.chart);
            }
        }
        world_.sceneObjects.push_back(std::move(o));
    }
}

MapObject Game::objectFootprint(const SceneObject& o) const {
    const auto& p = world_.placementOf(o);
    MapObject m;
    m.x = p.x;
    m.y = p.y;
    std::copy(std::begin(p.rect), std::end(p.rect), m.judge);
    return m;
}

void Game::drawSceneObject(const SceneObject& o, bool shadows, int camX, int camY, int shadowAlpha) {
    const auto& p = world_.placementOf(o);
    if (!p.look.drawn() || !world_.objectState(o, World::kShown)) return;
    // The one under the pointer is drawn brighter (0x40ee70); a chart's
    // brightness is set with the characters'.
    const int glow = hovered(Settings::kObject, o.placement) ? kHoverGlow : 0;
    const SDL_Color* outline = glow > 0 && world_.rules.outlineUnderPointer ? &kHoverOutline : nullptr;
    if (o.animation) {
        if (!shadows) drawCharacter(renderer_, sprites_, *o.animation, camX, camY, shadowAlpha, std::max(1, int{perMille(p.look.opacity)}));
        return;
    }
    if (!o.body) return;
    const int pattern = o.prop >= 0 ? o.prop : p.look.pattern;
    if (skin_ && o.prop < 0) {
        if (const auto* pieces = skin_->scenarioObject(p.kind, pattern)) {
            if (!shadows) skin_->renderScenarioObject(p.x, p.y, *pieces, camX, camY, glow, outline);
            return;
        }
    }
    const SpriteBank* bank = shadows ? o.shadow : o.body;
    if (!bank) return;
    const SpriteTexture* sprite = sprites_.get(*bank, pattern, false);
    if (!sprite) return;
    int sx, sy;
    worldToScreen(p.x, p.y, sx, sy);
    const SDL_Rect dst{sx - camX + sprite->originX, sy - camY + sprite->originY, sprite->width, sprite->height};
    if (dst.x + dst.w < 0 || dst.y + dst.h < 0 || dst.x >= viewWidth() || dst.y >= viewHeight()) return;
    if (shadows) {
        SDL_SetTextureColorMod(sprite->texture, 0, 0, 0);
        SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(shadowAlpha));
    } else {
        if (outline) drawOutline(*bank, pattern, sx - camX, sy - camY, *outline);
        SDL_SetTextureColorMod(sprite->texture, perMille(p.look.r), perMille(p.look.g), perMille(p.look.b));
        SDL_SetTextureAlphaMod(sprite->texture, perMille(p.look.opacity));
    }
    SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
    if (!shadows && glow > 0) {
        // A second copy added on top brightens it.
        SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(glow * perMille(p.look.opacity) / 1000));
        SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
    }
    SDL_SetTextureColorMod(sprite->texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(sprite->texture, 255);
}

void Game::drawOutline(const SpriteBank& bank, int pattern, int x, int y, SDL_Color color) {
    const SpriteTexture* ring = sprites_.outline(bank, pattern);
    if (!ring) return;
    const SDL_Rect dst{x + ring->originX, y + ring->originY, ring->width, ring->height};
    SDL_SetTextureColorMod(ring->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(ring->texture, color.a);
    SDL_RenderCopy(renderer_, ring->texture, nullptr, &dst);
}

SDL_Rect Game::sceneObjectPicture(const SceneObject& o) {
    const auto& p = world_.placementOf(o);
    if (o.animation) return characterBounds(sprites_, *o.animation);
    if (!o.body) return {};
    const int pattern = o.prop >= 0 ? o.prop : p.look.pattern;
    const auto* pieces = skin_ && o.prop < 0 ? skin_->scenarioObject(p.kind, pattern) : nullptr;
    if (pieces && pieces->empty()) return {};
    const SpriteTexture* sprite = sprites_.get(*o.body, pattern, false);
    int sx, sy;
    worldToScreen(p.x, p.y, sx, sy);
    if (!sprite) return pieces ? SDL_Rect{sx - 1024, sy - 1024, 2048, 2048} : SDL_Rect{};
    SDL_Rect picture{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
    if (pieces) picture = {picture.x - 32, picture.y - 32, picture.w + 64, picture.h + 64};
    return picture;
}

// CCHECK (0x4346b0): the left button is held on the object, which is shown
// and usable, and the hero stands within reach of its rectangle.
bool Game::workingObject(std::int32_t id) const {
    // The button itself, not mouseHeld_: a click on an object with a talk
    // script stops the hold from walking, but the button is still down.
    const bool down = pressed_ || (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) != 0;
    if (!player_ || (!down && heldObject_ != id)) return false;
    const auto* o = world_.sceneObject(id);
    if (!o || !world_.objectState(*o, World::kShown) || !world_.objectState(*o, World::kUsable)) return false;
    const auto& p = world_.placementOf(*o);
    if (heldObject_ != id) {
        const auto at = pointerPosition();
        if (!at) return false;
        double wx, wy;
        screenToWorld(pixelX(at->x), pixelY(at->y), wx, wy);
        if (!inside(p, wx, wy)) return false;
    }
    const double nx = std::clamp(player_->x, double(p.x + p.rect[0]), double(p.x + p.rect[2]));
    const double ny = std::clamp(player_->y, double(p.y + p.rect[1]), double(p.y + p.rect[3]));
    return std::hypot(nx - player_->x, ny - player_->y) <= kObjectReach;
}

}  // namespace sf
