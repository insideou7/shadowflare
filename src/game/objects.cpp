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
    sceneObjects_.clear();
    unlockSwitch_ = false;
    for (std::size_t i = 0; i < control_.placements.size(); ++i) {
        const auto& p = control_.placements[i];
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
                    log::warn("{}: no prop called {} in Map/props.sheet", scenarioDir_, p.look.prop);
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
                o.animation->restart(p.look.chart, 1.5);  // the original's 30 frames a second
            }
        }
        sceneObjects_.push_back(std::move(o));
    }
}

// Pictures change with the states alone; only charts move, and only while
// the object is shown (0x45ddd0 counts its frame there).
void Game::tickObjects() {
    for (auto& o : sceneObjects_) {
        if (o.animation && objectState(o, kShown)) o.animation->tick();
    }
    if (unlockSwitch_) unlockTicks_ += 1.5;
}

bool Game::objectState(const SceneObject& o, ObjectState which) const {
    if (o.fixed) return (*o.fixed)[which] != 0;
    const auto& p = placementOf(o);
    const auto found = flags_.find({ScenarioScript::kNet, kStateFlagBases[which] + ScenarioControl::idBase(p.group) + p.id});
    // A missing flag reads as -1, which counts as set (0x4541c0).
    return found == flags_.end() || found->second != 0;
}

const Game::SceneObject* Game::sceneObject(std::int32_t id) const {
    for (const auto& o : sceneObjects_) {
        const auto& p = placementOf(o);
        if (ScenarioControl::idBase(p.group) + p.id == id) return &o;
    }
    return nullptr;
}

Game::SceneObject* Game::sceneObject(std::int32_t id) { return const_cast<SceneObject*>(std::as_const(*this).sceneObject(id)); }

bool Game::objectInTheWay(double fromX, double fromY, double toX, double toY) const {
    for (const auto& o : sceneObjects_) {
        const auto& p = placementOf(o);
        if (p.rect[0] == p.rect[2] || p.rect[1] == p.rect[3]) continue;
        if (!objectState(o, kBlocks)) continue;
        if (inside(p, toX, toY) && !inside(p, fromX, fromY)) return true;
    }
    return false;
}

bool Game::boxMeetsObject(double x, double y, const JudgeRect& box) const {
    for (const auto& o : sceneObjects_) {
        const auto& p = placementOf(o);
        if (p.rect[0] == p.rect[2] || p.rect[1] == p.rect[3]) continue;
        if (!objectState(o, kBlocks)) continue;
        if (x + box.left <= p.x + p.rect[2] && p.x + p.rect[0] <= x + box.right && y + box.top <= p.y + p.rect[3] &&
            p.y + p.rect[1] <= y + box.bottom) {
            return true;
        }
    }
    return false;
}

MapObject Game::objectFootprint(const SceneObject& o) const {
    const auto& p = placementOf(o);
    MapObject m;
    m.x = p.x;
    m.y = p.y;
    std::copy(std::begin(p.rect), std::end(p.rect), m.judge);
    return m;
}

void Game::drawSceneObject(const SceneObject& o, bool shadows, int camX, int camY, int shadowAlpha) {
    const auto& p = placementOf(o);
    if (!p.look.drawn() || !objectState(o, kShown)) return;
    if (o.animation) {
        if (!shadows) o.animation->draw(renderer_, sprites_, camX, camY, shadowAlpha, std::max(1, int{perMille(p.look.opacity)}));
        return;
    }
    if (!o.body) return;
    const int pattern = o.prop >= 0 ? o.prop : p.look.pattern;
    if (skin_ && o.prop < 0) {
        if (const auto* pieces = skin_->scenarioObject(p.kind, pattern)) {
            if (!shadows) skin_->renderScenarioObject(p.x, p.y, *pieces, camX, camY);
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
        SDL_SetTextureColorMod(sprite->texture, perMille(p.look.r), perMille(p.look.g), perMille(p.look.b));
        SDL_SetTextureAlphaMod(sprite->texture, perMille(p.look.opacity));
    }
    SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
    SDL_SetTextureColorMod(sprite->texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(sprite->texture, 255);
}

// CCHECK (0x4346b0): the left button is held on the object, which is shown
// and usable, and the hero stands within reach of its rectangle.
bool Game::workingObject(std::int32_t id) const {
    if (!player_ || (!mouseHeld_ && heldObject_ != id)) return false;
    const auto* o = sceneObject(id);
    if (!o || !objectState(*o, kShown) || !objectState(*o, kUsable)) return false;
    const auto& p = placementOf(*o);
    if (heldObject_ != id) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, mx, my, &lx, &ly);
        double wx, wy;
        screenToWorld(pixelX(lx), pixelY(ly), wx, wy);
        if (!inside(p, wx, wy)) return false;
    }
    const double nx = std::clamp(player_->x, double(p.x + p.rect[0]), double(p.x + p.rect[2]));
    const double ny = std::clamp(player_->y, double(p.y + p.rect[1]), double(p.y + p.rect[3]));
    return std::hypot(nx - player_->x, ny - player_->y) <= kObjectReach;
}

}  // namespace sf
