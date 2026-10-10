#include "game/game.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>

#include "core/log.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

constexpr double kScrollPixelsPerTick = 24.0;

std::string stem(const std::string& file) {
    const auto dot = file.rfind('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

Game::Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap)
    : renderer_(renderer), data_(data), sprites_(renderer) {
    // Every level has a ground file; use those to enumerate maps.
    for (const auto& file : data_->list("Map/Ground")) mapNames_.push_back(stem(file));
    if (mapNames_.empty()) {
        log::warn("No maps found in the game data; showing a placeholder screen");
        return;
    }
    std::size_t start = 0;
    for (std::size_t i = 0; i < mapNames_.size(); ++i) {
        if (equalsIgnoreCase(mapNames_[i], startMap)) start = i;
    }
    loadMap(start);
}

void Game::loadMap(std::size_t index) {
    mapIndex_ = index;
    sprites_.clear();
    map_ = Map::load(*data_, mapNames_[index]);
    drawOrder_.clear();
    if (!map_) return;

    const auto& objects = map_->objects().objects;
    drawOrder_.resize(objects.size());
    for (std::size_t i = 0; i < objects.size(); ++i) drawOrder_[i] = i;
    // Painter's order: things lower on screen are drawn later.
    std::ranges::stable_sort(drawOrder_, [&](std::size_t a, std::size_t b) {
        return objects[a].x + objects[a].y < objects[b].x + objects[b].y;
    });
    centerCamera();
}

void Game::centerCamera() {
    const auto& objects = map_->objects().objects;
    const auto& g = map_->ground();
    double cx = g.width * g.chipWidth / 2.0, cy = g.height * g.chipHeight / 2.0;
    if (!objects.empty()) {
        int minX = INT_MAX, minY = INT_MAX, maxX = INT_MIN, maxY = INT_MIN;
        for (const auto& o : objects) {
            int sx, sy;
            worldToScreen(o.x, o.y, sx, sy);
            minX = std::min(minX, sx);
            maxX = std::max(maxX, sx);
            minY = std::min(minY, sy);
            maxY = std::max(maxY, sy);
        }
        cx = (minX + maxX) / 2.0;
        cy = (minY + maxY) / 2.0;
    }
    cameraX_ = cx - kScreenWidth / 2.0;
    cameraY_ = cy - kScreenHeight / 2.0;
}

void Game::handleEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: quit_ = true; break;
                case SDLK_PAGEDOWN:
                case SDLK_n:
                    if (!mapNames_.empty()) loadMap((mapIndex_ + 1) % mapNames_.size());
                    break;
                case SDLK_PAGEUP:
                case SDLK_p:
                    if (!mapNames_.empty()) loadMap((mapIndex_ + mapNames_.size() - 1) % mapNames_.size());
                    break;
                case SDLK_HOME:
                    if (map_) centerCamera();
                    break;
                default: break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) dragging_ = true;
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) dragging_ = false;
            break;
        case SDL_MOUSEMOTION:
            if (dragging_) {
                // Motion is reported in logical (640x480) coordinates.
                cameraX_ -= event.motion.xrel;
                cameraY_ -= event.motion.yrel;
            }
            break;
        default: break;
    }
}

void Game::tick() {
    ++ticks_;
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) cameraX_ -= kScrollPixelsPerTick;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) cameraX_ += kScrollPixelsPerTick;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) cameraY_ -= kScrollPixelsPerTick;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) cameraY_ += kScrollPixelsPerTick;
}

void Game::render(double interpolation) {
    if (map_) {
        renderMap();
    } else {
        renderPlaceholder(interpolation);
    }
    SDL_RenderPresent(renderer_);
}

void Game::renderMap() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);

    const int camX = static_cast<int>(std::floor(cameraX_));
    const int camY = static_cast<int>(std::floor(cameraY_));
    const auto& g = map_->ground();

    // Ground chips: only the ones overlapping the view.
    const int x0 = std::max(0, camX / g.chipWidth - 1);
    const int y0 = std::max(0, camY / g.chipHeight - 1);
    const int x1 = std::min(g.width, (camX + kScreenWidth) / g.chipWidth + 2);
    const int y1 = std::min(g.height, (camY + kScreenHeight) / g.chipHeight + 2);
    for (int cy = y0; cy < y1; ++cy) {
        for (int cx = x0; cx < x1; ++cx) {
            const auto& cell = g.at(cx, cy);
            const SpriteBank* bank = map_->bank(cell.bank);
            if (!bank) continue;
            const SpriteTexture* sprite = sprites_.get(*bank, cell.pattern, true);
            if (!sprite) continue;
            SDL_Rect dst{cx * g.chipWidth - camX, cy * g.chipHeight - camY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        }
    }

    // Objects, back to front. Shadows go first so nothing stands under one.
    const auto& objects = map_->objects().objects;
    for (int pass = 0; pass < 2; ++pass) {
        const bool shadows = pass == 0;
        for (std::size_t i : drawOrder_) {
            const MapObject& o = objects[i];
            if (map_->isShadowBank(o.bank) != shadows) continue;
            const SpriteBank* bank = map_->bank(o.bank);
            if (!bank) continue;
            int sx, sy;
            worldToScreen(o.x, o.y, sx, sy);
            sx -= camX;
            sy -= camY;
            if (sx < -1024 || sy < -1024 || sx > kScreenWidth + 1024 || sy > kScreenHeight + 1024) continue;
            const SpriteTexture* sprite = sprites_.get(*bank, o.pattern, false);
            if (!sprite) continue;
            SDL_Rect dst{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
            if (dst.x + dst.w < 0 || dst.y + dst.h < 0 || dst.x >= kScreenWidth || dst.y >= kScreenHeight) continue;
            if (shadows) {
                SDL_SetTextureColorMod(sprite->texture, 0, 0, 0);
                SDL_SetTextureAlphaMod(sprite->texture, 96);
            }
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        }
    }
}

void Game::renderPlaceholder(double interpolation) {
    // Shown when no game data is available: a slowly pulsing background.
    const double t = (static_cast<double>(ticks_) + interpolation) / kTicksPerSecond;
    const auto shade = static_cast<Uint8>(32 + 16 * std::sin(t));
    SDL_SetRenderDrawColor(renderer_, shade / 2, shade / 3, shade, 255);
    SDL_RenderClear(renderer_);
}

}  // namespace sf
