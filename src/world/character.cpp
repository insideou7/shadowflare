#include "world/character.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/map_layers.h"
#include "formats/open_content.h"
#include "world/content.h"
#include "platform/filesystem.h"

namespace sf {

std::optional<CharacterModel> CharacterModel::load(const FileSystem& data, const std::string& stem) {
    // Open content pack: a .anim file and the sheet it names.
    if (auto text = data.read(stem + ".anim")) {
        try {
            auto anim = open::parseAnimation(*text);
            // Trimmed: our renders are mostly see-through.
            auto body = loadSheet(data, folderOf(stem) + anim.sheet, true);
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

void Character::setModel(const CharacterModel* model) {
    model_ = model;
    if (!model_) return;
    if (!hasChart(chart_)) {
        chart_ = chart::kIdle;
        frame_ = 0;
        progress_ = 0;
    }
    const int count = frameCount();
    if (frame_ >= count) frame_ = std::max(count - 1, 0);
}

void Character::play(int chart) {
    if (chart != chart_) restart(chart);
}

void Character::restart(int chart, double speed) {
    chart_ = chart;
    frame_ = 0;
    speed_ = speed;
    progress_ = 0;
    cues_ = 0;
    strikes_ = 0;
    reached(0);
}

void Character::pose(int chart, int frame) {
    if (chart != chart_) {
        chart_ = chart;
        frame_ = -1;
        cues_ = 0;
        strikes_ = 0;
    }
    speed_ = 0;
    progress_ = 0;
    const auto* dir = currentDirection();
    const int last = dir ? std::max(0, dir->frameCount - 1) : 0;
    const int to = std::clamp(frame, 0, last);
    for (int f = frame_ + 1; f <= to; ++f) reached(f);
    frame_ = to;
}

void Character::reached(int frame) {
    const auto* dir = currentDirection();
    if (!dir) return;
    for (const auto& layer : dir->layers) {
        if (frame < static_cast<int>(layer.size())) cues_ |= layer[static_cast<std::size_t>(frame)].status & kCueFlags;
    }
    if (!dir->layers.empty() && frame < static_cast<int>(dir->layers[0].size()) && (dir->layers[0][static_cast<std::size_t>(frame)].status & 0x40)) {
        ++strikes_;
    }
}

bool Character::hasStrikes() const {
    const auto* dir = currentDirection();
    return dir && !dir->layers.empty() && std::ranges::any_of(dir->layers[0], [](const auto& cell) { return (cell.status & 0x40) != 0; });
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

void Character::setFrame(int frame) {
    const int count = frameCount();
    if (count <= 0) return;
    frame_ = std::clamp(frame, 0, count - 1);
    progress_ = 0;
    reached(frame_);
}

int Character::frameCountOf(int chart, int facing) const {
    const auto& charts = model_->animations.charts;
    if (chart < 0 || static_cast<std::size_t>(chart) >= charts.size()) return 0;
    const auto& dirs = charts[static_cast<std::size_t>(chart)].directions;
    if (facing < 0 || static_cast<std::size_t>(facing) >= dirs.size()) return 0;
    return dirs[static_cast<std::size_t>(facing)].frameCount;
}

bool Character::finished() const {
    const auto* dir = currentDirection();
    if (!dir || dir->frameCount <= 0) return true;
    if (model_->animations.charts[chart_].status == 1) return false;
    return frame_ >= dir->frameCount - 1;
}

void Character::faceTowards(double dx, double dy) {
    if (dx == 0 && dy == 0) return;
    direction = facingOf(dx, dy);
}

int facingOf(double dx, double dy) {
    // Work in screen space so "south" means down on screen.
    const double sx = (dx - dy) * projection.x;
    const double sy = (dx + dy) * projection.y;
    // Angle measured from screen-down, increasing towards screen-right (east).
    double angle = std::atan2(sx, sy);
    if (angle < 0) angle += 2 * std::numbers::pi;
    return static_cast<int>(std::lround(angle / (std::numbers::pi / 4))) % 8;
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
    if (held) return;
    const auto* dir = currentDirection();
    if (!dir || dir->frameCount <= 0) return;
    for (progress_ += speed_ * kFramesPerTick; progress_ >= 1.0; progress_ -= 1.0) {
        const int was = frame_;
        if (model_->animations.charts[chart_].status == 1) {
            frame_ = (frame_ + 1) % dir->frameCount;
        } else {
            frame_ = std::min(frame_ + 1, dir->frameCount - 1);
        }
        if (frame_ != was) reached(frame_);
    }
}

}  // namespace sf
