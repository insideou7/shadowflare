// The Create New Character screen that New Game opens, as ShadowFlare.exe
// runs it (0x423ca0): click the hero or the heroine, type a name, press OK
// (or Enter), then pick the game mode in the window that opens (0x424d80).
// Everything is drawn from System/Select/Pattern/Select.njp, whose
// patterns carry their own screen positions; the open content pack has its
// own Select.sheet with the same pattern numbers (tools/art/ui.py).
#include <algorithm>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {
namespace {

// Select.njp's patterns, each lit/dim pair as the exe uses them.
enum Pattern {
    kExit = 0, kExitLit = 1, kBack = 2, kBackLit = 3, kOk = 4, kOkLit = 5,
    kHero = 7, kHeroLit = 8, kHeroine = 9, kHeroineLit = 10, kHeroShade = 11, kHeroineShade = 12,
    kModeBack = 20, kModeBackLit = 21, kOnline = 22, kOnlineLit = 23, kSingle = 24, kSingleLit = 25,
    kName = 34, kNameLit = 35, kTitle = 36, kModeWindow = 38, kBackground = 41,
};

// Where a click counts, from the exe's mouse tests (left, top, right,
// bottom, exclusive).
struct Area {
    int l, t, r, b;
    bool contains(int x, int y) const { return x > l && x < r && y > t && y < b; }
};
constexpr Area kExitArea{575, 7, 633, 20};
constexpr Area kBackArea{392, 450, 466, 463};
constexpr Area kOkArea{566, 450, 604, 463};
constexpr Area kHeroArea{167, 106, 303, 379};
constexpr Area kHeroineArea{389, 106, 490, 375};
constexpr Area kOnlineArea{219, 207, 421, 220};
constexpr Area kSingleArea{221, 241, 418, 254};
constexpr Area kModeBackArea{283, 278, 357, 291};

// The figure not chosen fades and the chosen one slides to the middle, 97
// pixels, over 20 frames; Back undoes it the same way.
constexpr int kSlideTicks = 20;
constexpr int kSlide = 97;
constexpr int kFadeTicks = 20;   // the screen fading in, and out to the title
constexpr int kStartTicks = 14;  // fading out into the game (0x1392 to 0x13a0)
constexpr std::size_t kMaxName = 19;  // the manual: "less than 20 characters"

// Without any Select art: the words the patterns hold, and where.
struct Fallback {
    int pattern;
    const char* text;
    int x, y;
};
constexpr Fallback kFallback[] = {
    {kExit, "EXIT", 580, 8}, {kBack, "BACK", 396, 451}, {kOk, "OK", 570, 451},
    {kModeBack, "BACK", 290, 279}, {kOnline, "ONLINE MODE", 230, 208}, {kSingle, "SINGLE MODE", 232, 242},
    {kName, "NAME", 100, 411}, {kTitle, "CREATE NEW CHARACTER", 52, 24},
    {kHero, "HERO", 210, 230}, {kHeroine, "HEROINE", 410, 230},
    // The Load Data screen's (load_data.cpp).
    {6, "DELETE", 70, 451}, {14, "YES", 254, 276}, {16, "NO", 356, 276}, {37, "LOAD DATA", 52, 24},
};

int litOf(int pattern) {
    switch (pattern) {
        case kExitLit: return kExit;
        case kBackLit: return kBack;
        case kOkLit: return kOk;
        case kHeroLit: return kHero;
        case kHeroineLit: return kHeroine;
        case kModeBackLit: return kModeBack;
        case kOnlineLit: return kOnline;
        case kSingleLit: return kSingle;
        case kNameLit: return kName;
        case 13: return 6;   // Load Data's Delete
        case 15: return 14;  // and its Yes
        case 17: return 16;  // and No
        default: return -1;
    }
}

}  // namespace

void Game::setHero(HeroSex sex, std::string name) {
    heroSex_ = sex;
    heroName_ = std::move(name);
    if (player_) player_->setModel(heroModel(sex));
}

void Game::openNewCharacter() {
    newCharacter_.emplace();
    newCharacter_->hover = static_cast<int>(HeroSex::Male);
    newCharacter_->ticks = kFadeTicks;  // fading in
    mouseHeld_ = false;
}

void Game::tickNewCharacter() {
    auto& n = *newCharacter_;
    if (n.fade > 0 && n.fade < kSlideTicks) ++n.fade;
    if (n.slideBack > 0 && ++n.slideBack >= kSlideTicks) n.slideBack = 0;
    if (n.step == NewCharacter::Step::Starting) {
        if (++n.ticks >= kStartTicks) {
            const HeroSex sex = n.chosen;
            const std::string name = n.name;
            newCharacter_.reset();
            menuOpen_ = false;
            setHero(sex, name);
            newGame();
        }
    } else if (n.step == NewCharacter::Step::Leaving) {
        if (++n.ticks >= kFadeTicks) {
            newCharacter_.reset();
            openMenu();
        }
    } else if (n.ticks > 0) {
        --n.ticks;  // fading in
    }
}

void Game::newCharacterHover(int x, int y) {
    auto& n = *newCharacter_;
    using S = NewCharacter::Step;
    if (n.step == S::Choose && n.fade == 0 && n.slideBack == 0) {
        if (kHeroArea.contains(x, y)) n.hover = static_cast<int>(HeroSex::Male);
        if (kHeroineArea.contains(x, y)) n.hover = static_cast<int>(HeroSex::Female);
    } else if (n.step == S::Mode) {
        // Online play is not part of this game: the row stays dim.
        if (kSingleArea.contains(x, y)) n.hover = 1;
        if (kModeBackArea.contains(x, y)) n.hover = 2;
    }
}

void Game::newCharacterClick(int x, int y) {
    auto& n = *newCharacter_;
    using S = NewCharacter::Step;
    if (n.step == S::Mode) {
        if (kSingleArea.contains(x, y)) newCharacterKey(SDLK_RETURN);
        if (kModeBackArea.contains(x, y)) newCharacterKey(SDLK_ESCAPE);
        return;
    }
    if (n.step != S::Choose && n.step != S::Name) return;
    if (kExitArea.contains(x, y)) {
        playSound(0x37);  // (0x423ca0)
        quit_ = true;
    } else if (kBackArea.contains(x, y)) {
        newCharacterKey(SDLK_ESCAPE);
    } else if (n.step == S::Name && kOkArea.contains(x, y)) {
        if (!n.name.empty()) newCharacterKey(SDLK_RETURN);
    } else if (n.step == S::Choose && n.fade == 0 && n.slideBack == 0) {
        if (kHeroArea.contains(x, y) || kHeroineArea.contains(x, y)) {
            newCharacterHover(x, y);
            newCharacterKey(SDLK_RETURN);
        }
    }
}

void Game::newCharacterKey(SDL_Keycode key) {
    auto& n = *newCharacter_;
    using S = NewCharacter::Step;
    const bool back = key == SDLK_ESCAPE;
    const bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER;
    switch (n.step) {
        case S::Choose: {
            if (n.fade || n.slideBack) break;
            // Moving clicks with 0x3a when the choice changes, going on or
            // back with 0x37 (0x423ca0).
            const int was = n.hover;
            if (key == SDLK_LEFT || key == SDLK_UP) n.hover = static_cast<int>(HeroSex::Male);
            if (key == SDLK_RIGHT || key == SDLK_DOWN) n.hover = static_cast<int>(HeroSex::Female);
            if (n.hover != was) playSound(0x3a);
            if (enter || back) playSound(0x37);
            if (enter) {
                n.chosen = static_cast<HeroSex>(n.hover);
                n.step = S::Name;
                n.fade = 1;
                SDL_StartTextInput();
            } else if (back) {
                n.step = S::Leaving;
                n.ticks = 0;
            }
            break;
        }
        case S::Name:
            if (key == SDLK_BACKSPACE && !n.name.empty()) n.name.pop_back();
            // Enter with no name goes back to the figures, as the exe does
            // when its edit box closes empty.
            if (enter && !n.name.empty()) {
                playSound(0x37);
                n.step = S::Mode;
                n.hover = 1;
                SDL_StopTextInput();
            } else if (back || enter) {
                if (back) playSound(0x37);  // an empty name goes back silently
                n.step = S::Choose;
                n.fade = 0;
                n.slideBack = 1;
                n.hover = static_cast<int>(n.chosen);
                SDL_StopTextInput();
            }
            break;
        case S::Mode: {
            // Moving clicks with 0x3a when the choice changes; every way
            // out plays 0x38 (0x424d80).
            const int was = n.hover;
            if (key == SDLK_UP) n.hover = 1;
            if (key == SDLK_DOWN) n.hover = 2;
            if (n.hover != was) playSound(0x3a);
            if (back || enter) playSound(0x38);
            if (back || (enter && n.hover == 2)) {
                n.step = S::Name;  // back to the name, still typed in
                SDL_StartTextInput();
            } else if (enter) {
                n.step = S::Starting;
                n.ticks = 0;
                log::info("New {} named {}", n.chosen == HeroSex::Male ? "hero" : "heroine", n.name);
            }
            break;
        }
        case S::Starting:
        case S::Leaving: break;
    }
}

void Game::newCharacterEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_KEYDOWN: newCharacterKey(event.key.keysym.sym); break;
        case SDL_TEXTINPUT:
            if (newCharacter_->step == NewCharacter::Step::Name) {
                for (const char ch : std::string_view(event.text.text)) {
                    // The original's font has the printable ASCII range.
                    if (ch >= 32 && ch < 127 && newCharacter_->name.size() < kMaxName) newCharacter_->name += ch;
                }
            }
            break;
        case SDL_MOUSEMOTION: newCharacterHover(event.motion.x, event.motion.y); break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) newCharacterClick(event.button.x, event.button.y);
            break;
        default: break;
    }
}

// One of Select's patterns where it belongs on the screen, `dx` to the
// side; without the art, the words it holds.
void Game::newCharacterPattern(int pattern, int alpha, int dx, int dy, int brightness) {
    const SpriteBank* bank = itemBank("System/Select/Pattern/Select");
    if (const SpriteTexture* t = bank ? sprites_.get(*bank, pattern, pattern == kBackground) : nullptr) {
        const SDL_Rect dst{t->originX + dx, t->originY + dy, t->width, t->height};
        const auto b = static_cast<Uint8>(std::clamp(brightness, 0, 255));
        SDL_SetTextureAlphaMod(t->texture, static_cast<Uint8>(std::clamp(alpha, 0, 255)));
        SDL_SetTextureColorMod(t->texture, b, b, b);
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        SDL_SetTextureAlphaMod(t->texture, 255);
        SDL_SetTextureColorMod(t->texture, 255, 255, 255);
        return;
    }
    if (bank) return;
    if (pattern == kBackground) {
        SDL_SetRenderDrawColor(renderer_, 24, 26, 32, 255);
        SDL_RenderClear(renderer_);
        return;
    }
    if (pattern == kModeWindow) {
        const SDL_Rect r{176, 154, 288, 171};
        SDL_SetRenderDrawColor(renderer_, 30, 20, 20, 255);
        SDL_RenderFillRect(renderer_, &r);
        SDL_SetRenderDrawColor(renderer_, 150, 130, 90, 255);
        SDL_RenderDrawRect(renderer_, &r);
        return;
    }
    const int lit = litOf(pattern);
    const int base = lit >= 0 ? lit : pattern;
    for (const auto& f : kFallback) {
        if (f.pattern != base) continue;
        const Uint8 a = static_cast<Uint8>(std::clamp(alpha, 0, 255));
        const SDL_Color c = lit >= 0 ? SDL_Color{255, 230, 150, a} : SDL_Color{170, 150, 110, a};
        drawText(renderer_, f.x + dx, f.y + dy, f.text, c);
    }
}

void Game::renderNewCharacter() {
    const auto& n = *newCharacter_;
    using S = NewCharacter::Step;
    int mx = 0, my = 0;
    {
        int wx, wy;
        SDL_GetMouseState(&wx, &wy);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
        mx = static_cast<int>(lx);
        my = static_cast<int>(ly);
    }
    newCharacterPattern(kBackground);
    newCharacterPattern(kTitle);
    const bool choosing = n.step == S::Choose || n.step == S::Name;
    newCharacterPattern(choosing && kExitArea.contains(mx, my) ? kExitLit : kExit);
    newCharacterPattern(choosing && kBackArea.contains(mx, my) ? kBackLit : kBack);
    newCharacterPattern(n.step == S::Name && kOkArea.contains(mx, my) && !n.name.empty() ? kOkLit : kOk);

    // The two figures, each over its shade at half strength.
    const bool hero = n.chosen == HeroSex::Male;
    if (n.step == S::Choose) {
        int slide = 0, others = 255;
        if (n.slideBack > 0) {
            slide = kSlide - kSlide * n.slideBack / kSlideTicks;
            others = 255 * n.slideBack / kSlideTicks;
        }
        const int heroDx = hero ? slide : 0, heroineDx = hero ? 0 : -slide;
        const int heroAlpha = hero ? 255 : others, heroineAlpha = hero ? others : 255;
        const bool heroLit = n.slideBack == 0 && n.hover == static_cast<int>(HeroSex::Male);
        const bool heroineLit = n.slideBack == 0 && n.hover == static_cast<int>(HeroSex::Female);
        newCharacterPattern(heroLit ? kHeroLit : kHero, heroAlpha, heroDx);
        newCharacterPattern(kHeroShade, heroAlpha / 2, heroDx);
        newCharacterPattern(heroineLit ? kHeroineLit : kHeroine, heroineAlpha, heroineDx);
        newCharacterPattern(kHeroineShade, heroineAlpha / 2, heroineDx);
        newCharacterPattern(kName);
    } else {
        const int t = std::min(n.fade, kSlideTicks);
        const int slide = kSlide * t / kSlideTicks;
        const int fading = 255 - 255 * t / kSlideTicks;
        if (fading > 0) {
            newCharacterPattern(hero ? kHeroine : kHero, fading);
            newCharacterPattern(hero ? kHeroineShade : kHeroShade, fading / 2);
        }
        const int dx = hero ? slide : -slide;
        newCharacterPattern(hero ? kHeroLit : kHeroineLit, 255, dx);
        newCharacterPattern(hero ? kHeroShade : kHeroineShade, 128, dx);
        newCharacterPattern(kNameLit);
        // The name as it is typed, white over a black shadow, with a caret
        // while it can be edited.
        std::string shown = n.name;
        if (n.step == S::Name && (ticks_ / 10) % 2 == 0) shown += '_';
        uiText(195, 412, shown, {0, 0, 0, 255});
        uiText(194, 411, shown, {255, 255, 255, 255});
    }

    if (n.step == S::Mode || (n.step == S::Starting)) {
        newCharacterPattern(kModeWindow);
        newCharacterPattern(kOnline, 110);
        newCharacterPattern(n.hover == 1 ? kSingleLit : kSingle);
        newCharacterPattern(n.hover == 2 ? kModeBackLit : kModeBack);
    }

    // Fading in, out to the title, or out into the game.
    int dark = 0;
    if (n.step == S::Starting) dark = 255 * n.ticks / kStartTicks;
    else if (n.step == S::Leaving) dark = 255 * n.ticks / kFadeTicks;
    else dark = 255 * n.ticks / kFadeTicks;
    if (dark > 0) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, static_cast<Uint8>(std::clamp(dark, 0, 255)));
        SDL_Rect all{0, 0, kScreenWidth, kScreenHeight};
        SDL_RenderFillRect(renderer_, &all);
    }
}

}  // namespace sf
