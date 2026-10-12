// The map (N, or Map in the Settings Menu), as ShadowFlare.exe's window
// (drawn at 0x40d4d0; N opens and closes it in the key handler at
// 0x4429b0, as does the Settings Menu's MAP row at 0x4103c0): the area
// at a tenth of its size in the left half of the screen, black where the
// hero has not been, with the hero blinking in the middle and the area's
// name at the top. The arrow keys scroll it, 16 pixels across or 10 down
// a press, and Enter brings it back.
//
// The picture is the scenario's Scenario.Njp when the map is drawn with
// the original's art, and otherwise ours, drawn from the map itself at a
// tenth of its size the first time the map opens on it, so it shows what
// the player sees (the open content pack's maps, or the original's maps
// in our art). What has been seen is kept per area (ExploredMap) and goes
// in the saved game; walking reveals the block of 680 x 460 screen pixels
// round the hero, about the whole screen.
#include <algorithm>
#include <format>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "game/game.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

// Where the original draws it: the window clips to (32, 40)-(319, 375), the
// hero stands at (160, 210) of the screen before scrolling, the name box is
// at (68, 46) with the name at (72, 50).
constexpr SDL_Rect kWindow{32, 40, 288, 336};
constexpr int kHeroX = 160, kHeroY = 210;
constexpr int kNameX = 72, kNameY = 50;
// Clicks on the left half above the bar belong to the open map (0x4484c0).
constexpr int kClickRight = 320, kClickBottom = 412;
constexpr int kScrollX = 16, kScrollY = 10;
constexpr double kOriginalTicksPerTick = 30.0 / kTicksPerSecond;

// The pictures: Status.njp's frame (pattern 0x47) and glowing gem (0x76)
// with MapIcon.njp's arrow (0) and PLAYER (1), or our Map sheet's.
struct Parts {
    const char* frameBank;
    int frame, gem;
    const char* iconBank;
    int arrow, label;
};
constexpr Parts kOriginalParts{"System/Game/Pattern/Status", 0x47, 0x76, "System/Game/Pattern/MapIcon", 0, 1};
constexpr Parts kOpenParts{"System/Game/Pattern/Map", 0, 1, "System/Game/Pattern/Map", 2, 3};

}  // namespace

std::pair<int, int> Game::mapPosition(double worldX, double worldY) const {
    int sx, sy, left, top, right, bottom;
    worldToScreen(static_cast<int>(worldX), static_cast<int>(worldY), sx, sy);
    map_->ground().screenBounds(left, top, right, bottom);
    return {(sx - left) / kMapScale, (sy - top) / kMapScale};
}

std::string Game::exploredKey() const { return scenarioDir_.empty() ? "Map/" + mapNames_[mapIndex_] : scenarioDir_; }

ExploredMap& Game::explored() {
    int left, top, right, bottom;
    map_->ground().screenBounds(left, top, right, bottom);
    const int w = (right - left) / kMapScale, h = (bottom - top) / kMapScale;
    auto& e = explored_[exploredKey()];
    if (e.width != w || e.height != h) e = ExploredMap(w, h);  // new, or the map changed size
    return e;
}

void Game::toggleAreaMap() {
    if (mapOpen_) {
        closeAreaMap();
        return;
    }
    if (!map_ || !player_ || dialogue_ || openShop_) return;
    // It shares the left half with these.
    showStatus_ = false;
    closeMissions();
    closeMagicWindow();
    mapOpen_ = true;
    mapTicks_ = 0;
}

bool Game::areaMapKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_LEFT: mapScrollX_ -= kScrollX; return true;
        case SDLK_RIGHT: mapScrollX_ += kScrollX; return true;
        case SDLK_UP: mapScrollY_ -= kScrollY; return true;
        case SDLK_DOWN: mapScrollY_ += kScrollY; return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            mapScrollX_ = mapScrollY_ = 0;
            return true;
        default: return false;
    }
}

// The open map takes clicks on the left half above the bar; a right click
// there closes it (inferred: 0x44ad40 closes it from the right button's
// chain of handlers).
bool Game::areaMapClick(int x, int y, bool right) {
    if (!mapOpen_ || x >= kClickRight || y >= kClickBottom) return false;
    if (right) closeAreaMap();
    return true;
}

void Game::tickAreaMap() {
    if (!map_ || !player_) return;
    // The original reveals when the hero's screen position changes; the
    // block is the same wherever they stand, so revealing every tick is
    // the same.
    int sx, sy, left, top, right, bottom;
    worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), sx, sy);
    map_->ground().screenBounds(left, top, right, bottom);
    explored().reveal((sx - left - kRevealLeft) / kMapScale, (sy - top - kRevealTop) / kMapScale, kRevealWidth, kRevealHeight);
    if (mapOpen_) {
        mapTicks_ += kOriginalTicksPerTick;
    } else {
        mapTicks_ = 0;
    }
}

// Our picture of the area: the ground and objects drawn a screen at a time
// and each 10 x 10 block of pixels averaged into one.
void Game::buildAreaMapPicture() {
    mapPicture_.reset();
    mapPictureFor_ = mapNames_[mapIndex_];
    int left, top, right, bottom;
    map_->ground().screenBounds(left, top, right, bottom);
    const int w = (right - left) / kMapScale, h = (bottom - top) / kMapScale;
    if (w <= 0 || h <= 0) return;
    TexturePtr screen(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kScreenWidth, kScreenHeight));
    if (!screen) {
        log::warn("Map picture: {}", SDL_GetError());
        return;
    }
    SDL_Texture* previous = SDL_GetRenderTarget(renderer_);
    std::vector<std::uint32_t> small(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kScreenWidth) * kScreenHeight);
    constexpr int kCellsX = kScreenWidth / kMapScale, kCellsY = kScreenHeight / kMapScale;
    for (int cy = 0; cy < h; cy += kCellsY) {
        for (int cx = 0; cx < w; cx += kCellsX) {
            SDL_SetRenderTarget(renderer_, screen.get());
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            SDL_RenderClear(renderer_);
            renderScenery(left + cx * kMapScale, top + cy * kMapScale);
            if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), kScreenWidth * 4) != 0) continue;
            for (int y = 0; y < kCellsY && cy + y < h; ++y) {
                for (int x = 0; x < kCellsX && cx + x < w; ++x) {
                    unsigned r = 0, g = 0, b = 0;
                    for (int py = 0; py < kMapScale; ++py) {
                        const std::uint32_t* row = &pixels[static_cast<std::size_t>(y * kMapScale + py) * kScreenWidth + static_cast<std::size_t>(x * kMapScale)];
                        for (int px = 0; px < kMapScale; ++px) {
                            r += row[px] >> 16 & 0xff;
                            g += row[px] >> 8 & 0xff;
                            b += row[px] & 0xff;
                        }
                    }
                    constexpr unsigned n = kMapScale * kMapScale;
                    small[static_cast<std::size_t>(cy + y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(cx + x)] =
                        0xff000000u | (r / n) << 16 | (g / n) << 8 | (b / n);
                }
            }
        }
    }
    SDL_SetRenderTarget(renderer_, previous);
    mapPicture_.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h));
    if (!mapPicture_) return;
    SDL_UpdateTexture(mapPicture_.get(), nullptr, small.data(), w * 4);
    mapPictureW_ = w;
    mapPictureH_ = h;
}

// The area's picture with its top left corner of the map at (left, top).
void Game::drawAreaMapPicture(int left, int top) {
    // The original's own, for its maps in its own art.
    if (!skin_ && !scenarioDir_.empty()) {
        if (scenarioPictureFor_ != scenarioDir_) {
            scenarioPictureFor_ = scenarioDir_;
            scenarioPicture_.reset();
            try {
                if (auto raw = data_->read(scenarioDir_ + "/Scenario.Njp")) scenarioPicture_ = SpriteBank::parse(*raw);
            } catch (const FormatError& e) {
                log::warn("{}/Scenario.Njp: {}", scenarioDir_, e.what());
            }
        }
        if (scenarioPicture_) {
            if (const SpriteTexture* t = sprites_.get(*scenarioPicture_, 0, false)) {
                const SDL_Rect dst{left + t->originX, top + t->originY, t->width, t->height};
                SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
                return;
            }
        }
    }
    if (!mapPicture_ || mapPictureFor_ != mapNames_[mapIndex_]) buildAreaMapPicture();
    if (!mapPicture_) return;
    const SDL_Rect dst{left, top, mapPictureW_, mapPictureH_};
    SDL_RenderCopy(renderer_, mapPicture_.get(), nullptr, &dst);
}

void Game::renderAreaMap() {
    if (!mapOpen_ || !map_ || !player_) return;
    const auto [mx, my] = mapPosition(player_->x, player_->y);
    // Where the map's top left corner goes on the screen.
    const int left = kHeroX - mx - mapScrollX_, top = kHeroY - my - mapScrollY_;

    SDL_RenderSetClipRect(renderer_, &kWindow);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer_, &kWindow);
    drawAreaMapPicture(left, top);
    // Black where the hero has not been, a line at a time.
    const ExploredMap& seen = explored();
    for (int y = kWindow.y; y < kWindow.y + kWindow.h; ++y) {
        int x = kWindow.x;
        while (x < kWindow.x + kWindow.w) {
            const bool hidden = !seen.seenAt(x - left, y - top);
            int end = x + 1;
            while (end < kWindow.x + kWindow.w && !seen.seenAt(end - left, y - top) == hidden) ++end;
            if (hidden) SDL_RenderDrawLine(renderer_, x, y, end - 1, y);
            x = end;
        }
    }
    // The hero: the arrow and PLAYER, shown 15 ticks of every 20.
    const bool original = itemBank(kOriginalParts.frameBank) && itemBank(kOriginalParts.iconBank);
    const Parts& parts = original ? kOriginalParts : kOpenParts;
    auto draw = [&](const char* bankName, int pattern, int x, int y, Uint8 bright = 255) {
        const SpriteBank* bank = itemBank(bankName);
        const SpriteTexture* t = bank ? sprites_.get(*bank, pattern, false) : nullptr;
        if (!t) return;
        SDL_SetTextureColorMod(t->texture, bright, bright, bright);
        const SDL_Rect dst{x + t->originX, y + t->originY, t->width, t->height};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        SDL_SetTextureColorMod(t->texture, 255, 255, 255);
    };
    const int ticks = static_cast<int>(mapTicks_);
    if (ticks % 20 < 15) {
        draw(parts.iconBank, parts.arrow, mx + left, my + top);
        draw(parts.iconBank, parts.label, mx + left, my + top);
    }
    SDL_RenderSetClipRect(renderer_, nullptr);

    // The frame, and its gem glowing from 70% to full and back every 60 ticks.
    draw(parts.frameBank, parts.frame, 0, 0);
    const int pulse = ticks % 60;
    const int glow = pulse < 30 ? (pulse * 5 + 350) * 2 : (130 - pulse) * 10;
    draw(parts.frameBank, parts.gem, 0, 0, static_cast<Uint8>(std::clamp(glow * 255 / 1000, 0, 255)));

    // The area's name on a half-dark box.
    const std::string name = areaName_.substr(0, areaName_.find('\n'));
    if (!name.empty()) {
        const SDL_Rect box{kNameX - 4, kNameY - 4, static_cast<int>(name.size()) * 6 + 3, 21};
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 128);
        SDL_RenderFillRect(renderer_, &box);
        uiText(kNameX + 1, kNameY + 1, name, {0, 0, 0, 255});
        uiText(kNameX, kNameY, name, {224, 224, 224, 255});
    }
}

}  // namespace sf
