#pragma once

#include <SDL.h>

#include <string_view>

namespace sf {

// A built-in 3x5 pixel font for the stand-in interface (item names, gold,
// messages) until the original's fonts are read. Upper-case letters,
// digits and a little punctuation; lower case is drawn as upper case.
// Each pixel is `scale` screen pixels, with a dark outline.
void drawText(SDL_Renderer* renderer, int x, int y, std::string_view text, SDL_Color color, int scale = 2);
int textWidth(std::string_view text, int scale = 2);
inline int textHeight(int scale = 2) { return 5 * scale; }

}  // namespace sf
