// Missions: the mission list (Q), the newest mission named along the
// bottom of the screen with a lock to open the list, and the area's name
// by the compass at the top left. Laid out where ShadowFlare.exe draws
// them: the list at 0x40cea0, the bottom line at 0x4050f0 and the compass
// at 0x412220.

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "game/game.h"
#include "platform/filesystem.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

// The list: two pages of 24, each two columns of 12 rows. A row's lock is
// at (52 + 294 * column, 48 + 27 * row) and its title 27 pixels further
// right and 6 down; a click anywhere along the row opens its description.
constexpr int kPerPage = 24, kPerColumn = 12;
constexpr int kColumnX = 52, kColumnStep = 294, kRowY = 48, kRowStep = 27;
constexpr int kRowWidth = 294;
constexpr int kTitleDx = 27, kTitleDy = 6;
// The page buttons, I and II.
constexpr SDL_Rect kPageButtons[2] = {{276, 10, 20, 23}, {344, 10, 20, 23}};
// The description box and its words.
constexpr SDL_Rect kDescriptionBox{172, 76, 296, 260};
constexpr int kDescriptionX = 190, kDescriptionTitleY = 96, kDescriptionY = 120, kDescriptionStep = 16;
// The game area above the bottom bar: clicks there close things.
constexpr int kAboveBar = 412;

// The bottom line: the newest mission's title in brackets, its right end at
// x = 612, and a lock at (616, 360) while any mission is taken. Ours lifts
// both clear of the spell bar, which stands where the original has none.
constexpr int kNameRight = 612, kNameY = 368, kLockX = 616, kLockY = 360;
constexpr int kHudLift = 18;
// DRAWQUESTNAME shows the title for 600 of the original's ticks (20 s).
constexpr double kBannerTicks = 600;

// The compass plays its chart once, holds its last frame for 150 ticks,
// then fades over 30. The name is at (108, 32).
constexpr int kCompassHold = 150, kCompassFade = 30;
constexpr int kAreaNameX = 108, kAreaNameY = 32;

constexpr SDL_Color kTaken{224, 224, 224, 255};
constexpr SDL_Color kCleared{128, 128, 128, 255};
constexpr SDL_Color kTitle{224, 192, 0, 255};
constexpr SDL_Color kShadow{0, 0, 0, 255};

bool inside(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

// The pictures: the original's Status.njp, or our pack's Missions sheet,
// which lists them in this order.
struct Parts {
    int window, pageOff[2], pageOn[2], lockTaken, lockCleared, boxFill, boxFrame;
};
constexpr Parts kOriginalParts{10, {110, 111}, {112, 113}, 25, 26, 59, 58};
constexpr Parts kOpenParts{0, {1, 2}, {3, 4}, 5, 6, 7, 7};

int columnOf(int mission) { return (mission / kPerColumn) % 2; }
int rowOf(int mission) { return mission % kPerColumn; }
SDL_Rect rowRect(int mission) {
    return {kColumnX + columnOf(mission) * kColumnStep, kRowY + rowOf(mission) * kRowStep, kRowWidth, kRowStep};
}

// The compass's chart: chart 0, undirected (8), as 0x412220 asks for it.
const AnimationSet::Direction* compassDirection(const CharacterModel* m) {
    if (!m || m->animations.charts.empty()) return nullptr;
    const auto& chart = m->animations.charts[0];
    for (int d : {8, 0}) {
        if (static_cast<std::size_t>(d) < chart.directions.size() && chart.directions[static_cast<std::size_t>(d)].frameCount > 0) {
            return &chart.directions[static_cast<std::size_t>(d)];
        }
    }
    return nullptr;
}

int compassFrames(const CharacterModel* m) {
    const auto* dir = compassDirection(m);
    return dir ? dir->frameCount : 8;
}

}  // namespace

// Table.Tbd tables 41 and 700 on, or the pack's missions.txt.
void Game::loadMissions() {
    if (!tables_.tables.empty()) {
        missions_ = Missions::fromTables(tables_);
    } else if (auto text = data_->read("missions.txt")) {
        try {
            missions_ = open::parseMissions(*text);
        } catch (const FormatError& e) {
            log::warn("missions.txt: {}", e.what());
        }
    }
    for (auto& m : missions_.list) {
        m.title = toFontText(m.title);
        for (auto& l : m.lines) l = toFontText(l);
    }
    log::info("Loaded {} missions", missions_.count());
}

int Game::missionState(int mission) const {
    const auto it = world_.flags.find({ScenarioScript::kQuest, mission});
    return it == world_.flags.end() ? 0 : it->second;
}

bool Game::anyMissionTaken() const {
    for (int m = 0; m < missions_.count(); ++m) {
        if (missionState(m) == 1) return true;
    }
    return false;
}

void Game::showMissionName(int mission) {
    if (!missions_.find(mission)) return;
    missionBanner_ = mission;
    missionBannerTicks_ = kBannerTicks;
    log::info("Mission: {}", missions_.find(mission)->title);
}

void Game::showAreaName(std::string name) {
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) name.pop_back();
    areaName_ = std::move(name);
    compassAge_ = 0;
}

void Game::tickMissions() {
    if (missionBannerTicks_ > 0) missionBannerTicks_ = std::max(0.0, missionBannerTicks_ - kOriginalTicksPerTick);
    if (compassAge_ >= 0) {
        compassAge_ += kOriginalTicksPerTick;
        if (compassAge_ >= compassFrames(compassModel()) + kCompassHold + kCompassFade) compassAge_ = -1;
    }
}

void Game::openMissions(int shown) {
    if (!missionsOpen_) toggleMissions();
    if (shown >= 0 && missionState(shown) != 0) {
        missionShown_ = shown;
        missionPage_ = shown / kPerPage;
    }
}

// Q, the lock, the mission's name or the Settings Menu's row. Opening it
// closes the other windows (0x4170f0) and any description left open.
void Game::toggleMissions() {
    if (missionsOpen_) {
        closeMissions();
        return;
    }
    if (openShop_ || dialogue_) return;
    if (rightWindowOpen()) closeInventory();
    showStatus_ = false;
    closeMagicWindow();
    helpOpen_ = false;
    closeAreaMap();
    closeWarpWindow();
    missionsOpen_ = true;
    missionShown_ = -1;
}

void Game::closeMissions() {
    missionsOpen_ = false;
    missionShown_ = -1;
}

// A click while the list is open; true when it was used up. Everything
// above the bottom bar belongs to the list.
bool Game::missionsClick(int x, int y, bool right) {
    if (!missionsOpen_) return false;
    if (y >= kAboveBar) return false;
    if (right) {
        // A right click above the bar closes the list (inferred: 0x44ad10
        // closes it on a click above y = 412, from a handler that is not
        // the left button's).
        closeMissions();
        return true;
    }
    // With a description open, a click anywhere closes it.
    if (missionShown_ >= 0) {
        missionShown_ = -1;
        return true;
    }
    for (int page = 0; page < 2; ++page) {
        if (inside(kPageButtons[page], x, y)) {
            missionPage_ = page;
            playSound(0x3a);  // even when that page is showing (0x40cea0)
        }
    }
    const int first = missionPage_ * kPerPage;
    for (int m = first; m < std::min(first + kPerPage, missions_.count()); ++m) {
        if (missionState(m) != 0 && inside(rowRect(m), x, y)) missionShown_ = m;
    }
    return true;
}

// A click on the lock or on the mission's name toggles the list.
bool Game::missionHudClick(int x, int y) {
    if (missionsOpen_ || rightWindowOpen() || openShop_) return false;
    const int lift = kHudLift;
    if (anyMissionTaken() && x >= kLockX && x < kLockX + 24 && y >= kNameY - lift && y < kNameY + 16 - lift) {
        toggleMissions();
        return true;
    }
    if (missionBannerTicks_ > 0) {
        if (const auto* m = missions_.find(missionBanner_)) {
            const int w = static_cast<int>(m->title.size() + 2) * 6;
            if (x >= kNameRight - w && x < kNameRight && y >= kNameY - lift && y < kNameY + 12 - lift) {
                toggleMissions();
                return true;
            }
        }
    }
    return false;
}

void Game::renderMissions() {
    if (!missionsOpen_) return;
    // The original's pictures, our pack's, or plain boxes without either.
    const SpriteBank* bank = itemBank("System/Game/Pattern/Status");
    Parts parts = kOriginalParts;
    if (!bank || bank->patterns.size() <= 113) {
        bank = itemBank("System/Game/Pattern/Missions");
        parts = kOpenParts;
        if (bank && bank->patterns.size() <= 7) bank = nullptr;
    }
    auto draw = [&](int pattern, int x = 0, int y = 0) {
        const SpriteTexture* t = bank ? sprites_.get(*bank, pattern, false) : nullptr;
        if (!t) return false;
        const SDL_Rect dst{x + t->originX, y + t->originY, t->width, t->height};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        return true;
    };
    auto text = [&](int x, int y, std::string_view s, SDL_Color c) {
        uiText(x + 1, y + 1, s, kShadow);  // a shadow, as the exe draws text
        uiText(x, y, s, c);
    };
    auto box = [&](const SDL_Rect& r, SDL_Color fill, SDL_Color edge) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer_, &r);
        SDL_SetRenderDrawColor(renderer_, edge.r, edge.g, edge.b, edge.a);
        SDL_RenderDrawRect(renderer_, &r);
    };

    if (!draw(parts.window)) box({0, 0, kScreenWidth, kAboveBar + 3}, {44, 6, 6, 255}, {150, 130, 90, 255});
    for (int page = 0; page < 2; ++page) {
        if (draw(page == missionPage_ ? parts.pageOn[page] : parts.pageOff[page])) continue;
        const SDL_Rect& r = kPageButtons[page];
        box(r, page == missionPage_ ? SDL_Color{90, 70, 40, 255} : SDL_Color{40, 32, 24, 255}, {150, 130, 90, 255});
        drawText(renderer_, r.x + (r.w - textWidth(page ? "II" : "I")) / 2, r.y + 7, page ? "II" : "I", kTaken);
    }

    const int first = missionPage_ * kPerPage;
    for (int m = first; m < std::min(first + kPerPage, missions_.count()); ++m) {
        const int state = missionState(m);
        if (state == 0) continue;
        const SDL_Rect r = rowRect(m);
        if (!draw(state == 1 ? parts.lockTaken : parts.lockCleared, r.x, r.y)) {
            box({r.x + 5, r.y + 6, 12, 12}, state == 1 ? SDL_Color{160, 124, 66, 255} : SDL_Color{60, 60, 60, 255}, kShadow);
        }
        text(r.x + kTitleDx, r.y + kTitleDy, missions_.list[static_cast<std::size_t>(m)].title, state == 1 ? kTaken : kCleared);
    }

    if (const auto* m = missions_.find(missionShown_)) {
        // The original tints a dark red box purple under its frame; ours
        // draws the box in one.
        if (bank == itemBank("System/Game/Pattern/Status")) {
            const SDL_Rect fill{kDescriptionBox.x + 4, kDescriptionBox.y + 4, kDescriptionBox.w - 8, kDescriptionBox.h - 8};
            box(fill, {36, 6, 55, 255}, {36, 6, 55, 255});
            draw(parts.boxFrame);
        } else if (!draw(parts.boxFill)) {
            box(kDescriptionBox, {36, 6, 55, 255}, {200, 170, 90, 255});
        }
        text(kDescriptionX, kDescriptionTitleY, std::format("[{}]", m->title), kTitle);
        int y = kDescriptionY;
        for (const auto& line : m->lines) {
            text(kDescriptionX, y, line, kTaken);
            y += kDescriptionStep;
        }
    }
}

// The lock and the newest mission's name over the bottom right of the
// screen; hidden while a window covers that side.
void Game::renderMissionHud() {
    if (!player_ || missionsOpen_ || rightWindowOpen() || openShop_ || menuOpen_) return;
    const int lift = kHudLift;
    if (anyMissionTaken()) {
        // StatusIcon.njp pattern 0, or our pack's lock.
        const SpriteBank* icons = itemBank("System/Game/Pattern/StatusIcon");
        const SpriteTexture* lock = icons ? sprites_.get(*icons, 0, false) : nullptr;
        if (!lock) {
            const SpriteBank* ours = itemBank("System/Game/Pattern/Missions");
            lock = ours ? sprites_.get(*ours, kOpenParts.lockTaken, false) : nullptr;
        }
        if (lock) {
            const SDL_Rect dst{kLockX, kLockY - lift, lock->width, lock->height};
            SDL_RenderCopy(renderer_, lock->texture, nullptr, &dst);
        } else {
            SDL_SetRenderDrawColor(renderer_, 160, 124, 66, 255);
            const SDL_Rect r{kLockX, kLockY + 4 - lift, 12, 12};
            SDL_RenderFillRect(renderer_, &r);
        }
    }
    if (missionBannerTicks_ > 0) {
        if (const auto* m = missions_.find(missionBanner_)) {
            const std::string name = std::format("[{}]", m->title);
            const int x = kNameRight - static_cast<int>(name.size()) * 6;
            uiText(x + 1, kNameY + 1 - lift, name, kShadow);
            uiText(x, kNameY - lift, name, kTaken);
        }
    }
}

const CharacterModel* Game::compassModel() { return model("Player/Common/compasses"); }

// The compass at the top left of the screen with the area's name beside it,
// for a few seconds after arriving.
void Game::renderCompass() {
    if (compassAge_ < 0) return;
    const int age = static_cast<int>(compassAge_);
    const CharacterModel* m = compassModel();
    const AnimationSet::Direction* dir = compassDirection(m);
    const int frames = compassFrames(m);
    int alpha = 1000;
    if (age >= frames + kCompassHold) alpha = (frames + kCompassHold + kCompassFade - age) * 1000 / kCompassFade;
    if (alpha <= 0) return;
    const auto a = static_cast<Uint8>(std::clamp(alpha * 255 / 1000, 0, 255));
    if (dir) {
        const int frame = std::min(age, frames - 1);
        for (const auto& layer : dir->layers) {
            if (frame >= static_cast<int>(layer.size()) || layer[static_cast<std::size_t>(frame)].pattern < 0) continue;
            const auto& cell = layer[static_cast<std::size_t>(frame)];
            if (AnimationSet::isShadow(cell.status)) continue;
            const SpriteTexture* t = sprites_.get(m->body, cell.pattern, false);
            if (!t) continue;
            SDL_SetTextureBlendMode(t->texture, cell.status & AnimationSet::kAdditiveStatus ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(t->texture, a);
            const SDL_Rect dst{t->originX, t->originY, t->width, t->height};
            SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
            SDL_SetTextureAlphaMod(t->texture, 255);
        }
    }
    int y = kAreaNameY;
    std::size_t start = 0;
    while (start <= areaName_.size()) {
        auto end = areaName_.find('\n', start);
        if (end == std::string::npos) end = areaName_.size();
        const std::string_view line = std::string_view(areaName_).substr(start, end - start);
        uiText(kAreaNameX + 1, y + 1, line, {0, 0, 0, a});
        uiText(kAreaNameX, y, line, {224, 224, 224, a});
        y += 12;
        start = end + 1;
    }
}

}  // namespace sf
