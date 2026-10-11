#include "render/tiny_font.h"

#include <cctype>
#include <utility>
#include <vector>

namespace sf {

namespace {

// Five rows of three pixels, top row first.
const char* glyph(char c) {
    switch (std::toupper(static_cast<unsigned char>(c))) {
        case '0': return "111101101101111";
        case '1': return "010110010010111";
        case '2': return "111001111100111";
        case '3': return "111001111001111";
        case '4': return "101101111001001";
        case '5': return "111100111001111";
        case '6': return "111100111101111";
        case '7': return "111001001001001";
        case '8': return "111101111101111";
        case '9': return "111101111001111";
        case 'A': return "010101111101101";
        case 'B': return "110101110101110";
        case 'C': return "011100100100011";
        case 'D': return "110101101101110";
        case 'E': return "111100110100111";
        case 'F': return "111100110100100";
        case 'G': return "011100101101011";
        case 'H': return "101101111101101";
        case 'I': return "111010010010111";
        case 'J': return "001001001101010";
        case 'K': return "101101110101101";
        case 'L': return "100100100100111";
        case 'M': return "101111111101101";
        case 'N': return "110101101101101";
        case 'O': return "010101101101010";
        case 'P': return "110101110100100";
        case 'Q': return "010101101110011";
        case 'R': return "110101110101101";
        case 'S': return "011100010001110";
        case 'T': return "111010010010010";
        case 'U': return "101101101101111";
        case 'V': return "101101101101010";
        case 'W': return "101101111111101";
        case 'X': return "101101010101101";
        case 'Y': return "101101010010010";
        case 'Z': return "111001010100111";
        case ':': return "000010000010000";
        case '.': return "000000000000010";
        case ',': return "000000000010100";
        case '-': return "000000111000000";
        case '+': return "000010111010000";
        case '/': return "001001010100100";
        case '(': return "010100100100010";
        case ')': return "010001001001010";
        case '\'': return "010010000000000";
        case '!': return "010010010000010";
        case '?': return "111001010000010";
        case '%': return "101001010100101";
        default: return "000000000000000";
    }
}

void fill(SDL_Renderer* renderer, std::string_view text, int x, int y, int scale, std::vector<SDL_Rect>& rects) {
    rects.clear();
    for (char c : text) {
        const char* g = glyph(c);
        for (int i = 0; i < 15; ++i) {
            if (g[i] == '1') rects.push_back({x + (i % 3) * scale, y + (i / 3) * scale, scale, scale});
        }
        x += 4 * scale;
    }
    if (!rects.empty()) SDL_RenderFillRects(renderer, rects.data(), static_cast<int>(rects.size()));
}

}  // namespace

int textWidth(std::string_view text, int scale) { return text.empty() ? 0 : static_cast<int>(text.size()) * 4 * scale - scale; }

void drawText(SDL_Renderer* renderer, int x, int y, std::string_view text, SDL_Color color, int scale) {
    std::vector<SDL_Rect> rects;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    const int o = scale > 1 ? scale / 2 : 1;
    for (const auto& [dx, dy] : {std::pair{-o, 0}, {o, 0}, {0, -o}, {0, o}}) fill(renderer, text, x + dx, y + dy, scale, rects);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    fill(renderer, text, x, y, scale, rects);
}

}  // namespace sf
