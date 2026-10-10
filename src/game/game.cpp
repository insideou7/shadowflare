#include "game/game.h"

#include <cmath>

namespace sf {

Game::Game(SDL_Renderer* renderer, const FileSystem* data) : renderer_(renderer), data_(data) {}

void Game::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_QUIT) quit_ = true;
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) quit_ = true;
}

void Game::tick() {
    ++ticks_;
}

void Game::render(double interpolation) {
    // Placeholder until we can decode the original graphics: a slowly
    // pulsing background proves the loop and renderer are alive.
    const double t = (static_cast<double>(ticks_) + interpolation) / kTicksPerSecond;
    const auto shade = static_cast<Uint8>(32 + 16 * std::sin(t));
    SDL_SetRenderDrawColor(renderer_, shade / 2, shade / 3, shade, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderPresent(renderer_);
}

}  // namespace sf
