#pragma once

#include <SDL.h>

namespace sf {

class Character;
class SpriteCache;

// Draws a character's frame with its shadow, tint and glow. `shadowAlpha`
// is how dark the shadow is drawn. Below 255, `alpha` draws a see-through
// copy with no shadow.
void drawCharacter(SDL_Renderer* renderer, SpriteCache& sprites, const Character& character, int cameraX, int cameraY,
                   int shadowAlpha = 96, int alpha = 255);
// Where drawCharacter puts the picture (shadow left out), in map pixels;
// empty when it draws nothing.
SDL_Rect characterBounds(SpriteCache& sprites, const Character& character);

}  // namespace sf
