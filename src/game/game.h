#pragma once

#include <SDL.h>

#include <cstdint>

namespace sf {

class FileSystem;

// The original runs at 640x480; we render to that and let SDL scale it.
inline constexpr int kScreenWidth = 640;
inline constexpr int kScreenHeight = 480;

// Simulation runs at a fixed rate, separate from rendering, so game logic
// can later be matched tick-for-tick against the original executable.
inline constexpr int kTicksPerSecond = 20;

class Game {
public:
    Game(SDL_Renderer* renderer, const FileSystem* data);

    void handleEvent(const SDL_Event& event);
    void tick();
    void render(double interpolation);

    bool wantsQuit() const { return quit_; }
    std::uint64_t tickCount() const { return ticks_; }

private:
    SDL_Renderer* renderer_;
    const FileSystem* data_;
    std::uint64_t ticks_ = 0;
    bool quit_ = false;
};

}  // namespace sf
