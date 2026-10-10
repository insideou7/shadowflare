#pragma once

#include <SDL.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/map.h"
#include "render/sprite_cache.h"

namespace sf {

class FileSystem;

// The original runs at 640x480; we render to that and let SDL scale it.
inline constexpr int kScreenWidth = 640;
inline constexpr int kScreenHeight = 480;

// Simulation runs at a fixed rate, separate from rendering, so game logic
// can later be matched tick-for-tick against the original executable.
inline constexpr int kTicksPerSecond = 20;

// For now the game is a map viewer: it loads a level from the original
// data and lets you scroll around it.
class Game {
public:
    Game(SDL_Renderer* renderer, const FileSystem* data, const std::string& startMap);

    void handleEvent(const SDL_Event& event);
    void tick();
    void render(double interpolation);

    bool wantsQuit() const { return quit_; }
    std::uint64_t tickCount() const { return ticks_; }

private:
    void loadMap(std::size_t index);
    void centerCamera();
    void renderMap();
    void renderPlaceholder(double interpolation);

    SDL_Renderer* renderer_;
    const FileSystem* data_;
    SpriteCache sprites_;
    std::vector<std::string> mapNames_;
    std::size_t mapIndex_ = 0;
    std::optional<Map> map_;
    std::vector<std::size_t> drawOrder_;  // objects sorted back to front

    double cameraX_ = 0, cameraY_ = 0;  // top-left of the view, in map pixels
    bool dragging_ = false;
    std::uint64_t ticks_ = 0;
    bool quit_ = false;
};

}  // namespace sf
