#include "game/character.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/map_layers.h"
#include "formats/open_content.h"
#include "game/content.h"
#include "platform/filesystem.h"
#include "render/sprite_cache.h"

namespace sf {

std::optional<CharacterModel> CharacterModel::load(const FileSystem& data, const std::string& stem) {
    // Open content pack: a .anim file and the sheet it names.
    if (auto text = data.read(stem + ".anim")) {
        try {
            auto anim = open::parseAnimation(*text);
            auto body = loadSheet(data, folderOf(stem) + anim.sheet);
            if (!body) return std::nullopt;
            CharacterModel model;
            model.animations = std::move(anim.set);
            model.body = std::move(*body);
            return model;
        } catch (const FormatError& e) {
            log::error("Character {}: {}", stem, e.what());
            return std::nullopt;
        }
    }
    auto caf = data.read(stem + ".caf");
    auto njp = data.read(stem + ".njp");
    if (!caf || !njp) {
        log::error("Character {} is missing its .caf or .njp file", stem);
        return std::nullopt;
    }
    try {
        CharacterModel model;
        model.animations = AnimationSet::parse(*caf);
        model.body = SpriteBank::parse(*njp);
        if (auto sdw = data.read(stem + ".sdw")) model.shadow = SpriteBank::parse(*sdw);
        return model;
    } catch (const FormatError& e) {
        log::error("Character {}: {}", stem, e.what());
        return std::nullopt;
    }
}

void Character::play(int chart) {
    if (chart != chart_) restart(chart);
}

void Character::restart(int chart) {
    chart_ = chart;
    frame_ = 0;
}

bool Character::hasChart(int chart) const {
    const auto& charts = model_->animations.charts;
    if (chart < 0 || static_cast<std::size_t>(chart) >= charts.size()) return false;
    const auto& dirs = charts[chart].directions;
    return !dirs.empty() && dirs[0].frameCount > 0;
}

int Character::frameCount() const {
    const auto* dir = currentDirection();
    return dir ? dir->frameCount : 0;
}

bool Character::finished() const {
    const auto* dir = currentDirection();
    if (!dir || dir->frameCount <= 0) return true;
    if (model_->animations.charts[chart_].status == 1) return false;
    return frame_ >= dir->frameCount - 1;
}

void Character::faceTowards(double dx, double dy) {
    if (dx == 0 && dy == 0) return;
    // Work in screen space so "south" means down on screen.
    const double sx = (dx - dy) * projection.x;
    const double sy = (dx + dy) * projection.y;
    // Angle measured from screen-down, increasing towards screen-right (east).
    double angle = std::atan2(sx, sy);
    if (angle < 0) angle += 2 * std::numbers::pi;
    direction = static_cast<int>(std::lround(angle / (std::numbers::pi / 4))) % 8;
}

const AnimationSet::Direction* Character::currentDirection() const {
    const auto& charts = model_->animations.charts;
    const auto chart = static_cast<std::size_t>(chart_);
    if (chart >= charts.size()) return nullptr;
    const auto& dirs = charts[chart].directions;
    if (direction < 0 || static_cast<std::size_t>(direction) >= dirs.size()) return nullptr;
    return &dirs[direction];
}

void Character::tick() {
    const auto* dir = currentDirection();
    if (!dir || dir->frameCount <= 0) return;
    if (model_->animations.charts[chart_].status == 1) {
        frame_ = (frame_ + 1) % dir->frameCount;
    } else {
        frame_ = std::min(frame_ + 1, dir->frameCount - 1);
    }
}

void Character::draw(SDL_Renderer* renderer, SpriteCache& sprites, int cameraX, int cameraY) const {
    const auto* dir = currentDirection();
    if (!dir) return;
    int sx, sy;
    worldToScreen(static_cast<int>(x), static_cast<int>(y), sx, sy);
    sx -= cameraX;
    sy -= cameraY;

    struct Item {
        const AnimationSet::Cell* cell;
        bool shadow;
    };
    std::vector<Item> items;
    for (std::size_t i = 0; i < dir->layers.size(); ++i) {
        const auto& layer = dir->layers[i];
        if (frame_ >= static_cast<int>(layer.size())) continue;
        const auto& cell = layer[frame_];
        const bool shadow = cell.status == AnimationSet::kShadowStatus;
        if (!shadow && std::ranges::find(visibleLayers, static_cast<int>(i)) == visibleLayers.end()) continue;
        items.push_back({&cell, shadow});
    }
    // Shadows first, then the rest by priority.
    std::ranges::stable_sort(items, [](const Item& a, const Item& b) {
        if (a.shadow != b.shadow) return a.shadow;
        return a.cell->priority < b.cell->priority;
    });

    for (const auto& item : items) {
        const SpriteBank& bank = item.shadow ? model_->shadow : model_->body;
        const SpriteTexture* sprite = sprites.get(bank, item.cell->pattern, false);
        if (!sprite) continue;
        if (item.shadow) {
            SDL_SetTextureColorMod(sprite->texture, 0, 0, 0);
            SDL_SetTextureAlphaMod(sprite->texture, 96);
        }
        SDL_Rect dst{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
        SDL_RenderCopy(renderer, sprite->texture, nullptr, &dst);
    }
}

}  // namespace sf
