#include "game/game.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>

#include "core/log.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

// Walking speed in world units per tick. Not yet taken from the original.
constexpr double kWalkSpeed = 70.0;

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
    playerModel_ = CharacterModel::load(*data_, "Player/Male/Animation00");
    if (playerModel_) player_.emplace(&*playerModel_);
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
    placePlayer();
}

// Puts the player in the middle of the map's scenery and points the camera at them.
void Game::placePlayer() {
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
    walkTarget_.reset();
    if (player_) {
        screenToWorld(cx, cy, player_->x, player_->y);
        player_->setAction(Action::Idle);
    }
    cameraX_ = cx - kScreenWidth / 2.0;
    cameraY_ = cy - kScreenHeight / 2.0;
}

void Game::walkBy(int screenDx, int screenDy) {
    if (!player_) return;
    double dx, dy;
    screenToWorld(screenDx, screenDy, dx, dy);
    walkTarget_.emplace(player_->x + dx, player_->y + dy);
}

void Game::updateCamera() {
    if (!player_) return;
    int sx, sy;
    worldToScreen(static_cast<int>(player_->x), static_cast<int>(player_->y), sx, sy);
    cameraX_ = sx - kScreenWidth / 2.0;
    cameraY_ = sy - kScreenHeight / 2.0;
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
                    if (map_) placePlayer();
                    break;
                default: break;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) mouseHeld_ = true;
            break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) mouseHeld_ = false;
            break;
        default: break;
    }
}

void Game::tick() {
    ++ticks_;
    if (!player_) return;

    // Holding the left button walks towards the cursor, like the original.
    if (mouseHeld_) {
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, mx, my, &lx, &ly);
        double wx, wy;
        screenToWorld(cameraX_ + lx, cameraY_ + ly, wx, wy);
        walkTarget_.emplace(wx, wy);
    }

    // Keyboard movement in screen directions.
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    double kx = 0, ky = 0;
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) kx -= 1;
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) kx += 1;
    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) ky -= 1;
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) ky += 1;

    double dx = 0, dy = 0;
    if (kx != 0 || ky != 0) {
        walkTarget_.reset();
        // A screen-space step converted to world space.
        screenToWorld(kx * 0.15, ky * 0.10, dx, dy);
    } else if (walkTarget_) {
        dx = walkTarget_->first - player_->x;
        dy = walkTarget_->second - player_->y;
    }

    const double length = std::hypot(dx, dy);
    if (length > 1.0) {
        const double step = std::min(kWalkSpeed, walkTarget_ ? length : kWalkSpeed);
        player_->x += dx / length * step;
        player_->y += dy / length * step;
        player_->faceTowards(dx, dy);
        player_->setAction(Action::Walk);
        if (walkTarget_ && length <= kWalkSpeed) walkTarget_.reset();
    } else {
        walkTarget_.reset();
        player_->setAction(Action::Idle);
    }
    player_->tick();
    updateCamera();
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
    // The player is drawn in the second pass at their place in the order.
    const auto& objects = map_->objects().objects;
    const double playerDepth = player_ ? player_->x + player_->y : 0;
    for (int pass = 0; pass < 2; ++pass) {
        const bool shadows = pass == 0;
        bool playerDrawn = !player_ || shadows;
        for (std::size_t i : drawOrder_) {
            const MapObject& o = objects[i];
            if (!playerDrawn && o.x + o.y > playerDepth) {
                player_->draw(renderer_, sprites_, camX, camY);
                playerDrawn = true;
            }
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
        if (!playerDrawn) player_->draw(renderer_, sprites_, camX, camY);
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
