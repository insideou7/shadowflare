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
// tenth of its size, so it shows what the player sees. The open content
// pack brings its maps' pictures drawn ahead of time (Map/Overview, made
// with --render-maps). The original's maps in our art are drawn the first
// time each opens and kept in the settings folder's maps/, by the art they
// were drawn in. What has been seen is kept per area (ExploredMap) and goes
// in the saved game; walking reveals the block of 680 x 460 screen pixels
// round the hero, about the whole screen.
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "game/game.h"
#include "game/skin.h"
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
// The most our picture of the area is drawn in at once, in pixels each way.
constexpr int kMaxPiece = 4096;

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
    closeWarpWindow();
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

// Our picture of the area: the ground and objects drawn at full size, as
// big a piece at a time as the renderer takes (reading a picture back from
// the graphics card stalls it, so the fewer pieces the better), and each
// 10 x 10 block of pixels averaged into one. Our ground on the original's
// maps is painted pixel by pixel, which for a whole map takes long enough
// to freeze the game, so there the skin works out each block's average
// ground without painting it, and only the objects are drawn, on nothing,
// to go over it.
std::vector<std::uint32_t> Game::drawAreaMapPixels(int& w, int& h) {
    int left, top, right, bottom;
    map_->ground().screenBounds(left, top, right, bottom);
    w = (right - left) / kMapScale;
    h = (bottom - top) / kMapScale;
    if (w <= 0 || h <= 0) return {};
    SDL_RendererInfo info{};
    SDL_GetRendererInfo(renderer_, &info);
    auto pieceSize = [](int most, int need) {
        if (most <= 0 || most > kMaxPiece) most = kMaxPiece;
        return std::max(kMapScale, std::min(most, need) / kMapScale * kMapScale);
    };
    const int pieceW = pieceSize(info.max_texture_width, w * kMapScale), pieceH = pieceSize(info.max_texture_height, h * kMapScale);
    TexturePtr piece(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, pieceW, pieceH));
    // Each piece is halved on the graphics card before it is read back: at
    // exactly half the size, smooth scaling averages each 2 x 2 pixels.
    static_assert(kMapScale % 2 == 0);
    constexpr int kHalfScale = kMapScale / 2;
    const int halfW = pieceW / 2, halfH = pieceH / 2;
    TexturePtr half(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, halfW, halfH));
    if (!piece || !half) {
        log::warn("Map picture: {}", SDL_GetError());
        return {};
    }
    SDL_SetTextureBlendMode(piece.get(), SDL_BLENDMODE_NONE);
    SDL_SetTextureScaleMode(piece.get(), SDL_ScaleModeLinear);
    const bool skinGround = skin_ && map_->ground().isoTile <= 0;
    std::vector<std::uint32_t> small =
        skinGround ? skin_->groundOverview(left, top, kMapScale, w, h)
                   : std::vector<std::uint32_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    SDL_Texture* previous = SDL_GetRenderTarget(renderer_);
    fullSize_ = true;  // at full size, a piece at a time
    shotWidth_ = pieceW;
    shotHeight_ = pieceH;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(halfW) * static_cast<std::size_t>(halfH));
    const int cellsX = pieceW / kMapScale, cellsY = pieceH / kMapScale;
    constexpr unsigned n = kHalfScale * kHalfScale;
    for (int cy = 0; cy < h; cy += cellsY) {
        for (int cx = 0; cx < w; cx += cellsX) {
            SDL_SetRenderTarget(renderer_, piece.get());
            // Clear, for the objects to go over the ground's picture: what
            // they cover comes out in their colours times their alpha.
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, skinGround ? 0 : 255);
            SDL_RenderClear(renderer_);
            renderScenery(left + cx * kMapScale, top + cy * kMapScale, !skinGround);
            SDL_SetRenderTarget(renderer_, half.get());
            SDL_RenderCopy(renderer_, piece.get(), nullptr, nullptr);
            if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, pixels.data(), halfW * 4) != 0) continue;
            for (int y = 0; y < cellsY && cy + y < h; ++y) {
                for (int x = 0; x < cellsX && cx + x < w; ++x) {
                    // Two channels to a sum, 16 bits each: 25 pixels of
                    // 255 fit.
                    std::uint32_t redBlue = 0, alphaGreen = 0;
                    for (int py = 0; py < kHalfScale; ++py) {
                        const std::uint32_t* row = &pixels[static_cast<std::size_t>(y * kHalfScale + py) * static_cast<std::size_t>(halfW) + static_cast<std::size_t>(x * kHalfScale)];
                        for (int px = 0; px < kHalfScale; ++px) {
                            redBlue += row[px] & 0x00ff00ffu;
                            alphaGreen += row[px] >> 8 & 0x00ff00ffu;
                        }
                    }
                    unsigned r = redBlue >> 16, g = alphaGreen & 0xffff, b = redBlue & 0xffff;
                    const unsigned a = alphaGreen >> 16;
                    std::uint32_t& out = small[static_cast<std::size_t>(cy + y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(cx + x)];
                    if (skinGround) {
                        // The ground shows through where the objects leave it.
                        const unsigned under = 255 * n - a;
                        r = (r * 255 + (out >> 16 & 0xff) * under) / 255;
                        g = (g * 255 + (out >> 8 & 0xff) * under) / 255;
                        b = (b * 255 + (out & 0xff) * under) / 255;
                    }
                    out = 0xff000000u | std::min(r / n, 255u) << 16 | std::min(g / n, 255u) << 8 | std::min(b / n, 255u);
                }
            }
        }
    }
    shotWidth_ = shotHeight_ = 0;
    SDL_SetRenderTarget(renderer_, previous);
    fullSize_ = false;
    return small;
}

std::string Game::areaMapPictureFile() {
    if (mapPictureFolder_.empty() || data_->exists("content.txt")) return {};
    // Kept apart by the art they are drawn in, and by how they are drawn.
    constexpr int kVersion = 1;
    std::string art = "original";
    if (skin_) {
        if (!skinFingerprint_) skinFingerprint_ = skin_->fingerprint();
        art = std::format("ours-{:016x}", *skinFingerprint_);
    }
    return (std::filesystem::path(mapPictureFolder_) / std::format("{}-{}", art, kVersion) / (mapNames_[mapIndex_] + ".png")).string();
}

// The picture comes from the open pack's Map/Overview, else from the ones
// kept from earlier runs, else it is drawn now (and kept).
void Game::buildAreaMapPicture() {
    mapPicture_.reset();
    mapPictureFor_ = mapNames_[mapIndex_];
    int left, top, right, bottom;
    map_->ground().screenBounds(left, top, right, bottom);
    const int wantW = (right - left) / kMapScale, wantH = (bottom - top) / kMapScale;
    const std::string file = areaMapPictureFile();
    std::optional<std::vector<std::uint8_t>> png;
    if (!skin_ && data_->exists("content.txt")) {
        png = data_->read("Map/Overview/" + mapPictureFor_ + ".png");
    } else if (!file.empty()) {
        std::ifstream in(file, std::ios::binary);
        if (in) png = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
    }
    std::vector<std::uint32_t> pixels;
    int w = 0, h = 0;
    if (png) {
        try {
            const auto image = open::decodeImage(*png);
            if (image.width == wantW && image.height == wantH) {
                pixels.assign(image.argb.begin(), image.argb.end());
                w = image.width;
                h = image.height;
            }
        } catch (const FormatError& e) {
            log::warn("Map picture of {}: {}", mapPictureFor_, e.what());
        }
    }
    if (pixels.empty()) {
        pixels = drawAreaMapPixels(w, h);
        if (pixels.empty()) return;
        if (!file.empty()) {
            std::error_code error;
            std::filesystem::create_directories(std::filesystem::path(file).parent_path(), error);
            const auto bytes = open::encodePng(pixels, w, h);
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out) log::warn("Could not keep the map picture in {}", file);
        }
    }
    mapPicture_.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h));
    if (!mapPicture_) return;
    SDL_UpdateTexture(mapPicture_.get(), nullptr, pixels.data(), w * 4);
    mapPictureW_ = w;
    mapPictureH_ = h;
}

int Game::renderMapPictures(const std::string& folder) {
    int made = 0;
    for (std::size_t i = 0; i < mapNames_.size(); ++i) {
        loadMapLayers(i);
        if (!map_) continue;
        const std::filesystem::path path = folder.empty() ? std::filesystem::path(areaMapPictureFile()) : std::filesystem::path(folder) / (mapNames_[i] + ".png");
        if (path.empty()) {
            log::warn("No folder to keep the map pictures in");
            break;
        }
        int w = 0, h = 0;
        const auto pixels = drawAreaMapPixels(w, h);
        if (pixels.empty()) continue;
        const auto bytes = open::encodePng(pixels, w, h);
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            log::warn("Could not write {}", path.string());
            continue;
        }
        ++made;
    }
    mapPicture_.reset();
    mapPictureFor_.clear();
    return made;
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
