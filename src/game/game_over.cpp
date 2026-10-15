// What happens when the hero falls. ShadowFlare.exe drops them where they
// stand (0x435b60, run each tick while their health is gone): the death
// sound, the parting glow (controller 0x5212, option 11000023) and chart 4
// facing nowhere (direction 8). Once its last frame has been held for 120
// ticks it takes half their gold and sends them to the area's town
// (scenario + 0x60, at entry point scenario + 0x64), where 0x426200 wakes
// them with full health and mana (0x440c20) and counts the death (+ 0x15d8).
//
// Ours (Rules::gameOverWindow) opens a Game Over window there instead: carry
// on from the hero's last save, or go back to the title. A hero with no save
// yet carries on as the original does, in town.
#include <filesystem>
#include <format>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr int kDeathChart = 4;
constexpr int kNoDirection = 8;   // the death chart's only direction
constexpr int kPartingGlow = 11000023;
constexpr double kHoldTicks = 120;  // the original's ticks on the last frame (0x435d9e: more than 0x77)

// The window: a panel in the middle of the screen with its two choices.
constexpr SDL_Rect kPanel{170, 150, 300, 170};
constexpr SDL_Rect kContinue{200, 222, 240, 30};
constexpr SDL_Rect kExit{200, 270, 240, 30};

const SDL_Color kTitle{220, 60, 50, 255};
const SDL_Color kLabel{225, 220, 205, 255};
const SDL_Color kLit{255, 210, 80, 255};
const SDL_Color kNote{150, 140, 125, 255};

bool on(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

void box(SDL_Renderer* renderer, const SDL_Rect& r, SDL_Color fill, SDL_Color edge) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, edge.r, edge.g, edge.b, edge.a);
    SDL_RenderDrawRect(renderer, &r);
}

}  // namespace

void Game::fall() {
    if (fallTicks_ || !player_) return;
    log::info("The player was defeated (tick {})", ticks_);
    fallTicks_ = 0.0;
    playSound(heroSex_ == HeroSex::Female ? 14 : 13, player_->x, player_->y);  // 0x435b60
    // Whatever the hero was doing stops (0x451d60, 0x452910, 0x450080).
    walkTarget_.reset();
    attackTarget_.reset();
    mouseHeld_ = false;
    casting_.reset();
    combo_.reset();
    if (effectModel(kPartingGlow)) spawnEffect(kPartingGlow, player_->x, player_->y).followsPlayer = true;
    if (player_->frameCountOf(kDeathChart, kNoDirection) > 0) {
        player_->direction = kNoDirection;
        player_->restart(kDeathChart);
    } else {
        player_->restart(chart::kPlayerHit);
    }
}

void Game::tickFall() {
    player_->tick();
    if (!player_->finished()) return;
    *fallTicks_ += kOriginalTicksPerTick;
    if (*fallTicks_ <= kHoldTicks) return;
    if (rules_.gameOverWindow) {
        gameOverOpen_ = true;
        gameOverChoice_ = 0;
        log::info("Game Over (tick {})", ticks_);
    } else {
        wakeInTown();
    }
}

void Game::wakeInTown() {
    fallTicks_.reset();
    gameOverOpen_ = false;
    const auto gold = inventory_.gold();
    const int lost = static_cast<int>(gold - gold / 2);
    inventory_.addGold(-lost);
    playerHp_ = playerMaxHp();
    playerMp_ = playerMaxMp();
    player_->direction = 0;
    player_->restart(chart::kIdle);
    log::info("The player wakes in town (scenario {}, entry {}), {} gold lost", control_.town, control_.townEntry, lost);
    pendingScenario_.emplace(control_.town, control_.townEntry);
}

bool Game::heroHasSave() const {
    std::error_code ec;
    return !saveFile_.empty() && std::filesystem::exists(saveFile_, ec);
}

void Game::chooseGameOver(bool carryOn) {
    playSound(0x38);
    if (!carryOn) {
        fallTicks_.reset();
        gameOverOpen_ = false;
        openMenu();
        return;
    }
    if (heroHasSave()) {
        fallTicks_.reset();
        gameOverOpen_ = false;
        if (loadGameFrom(saveFile_)) {
            if (player_) {
                player_->direction = 0;
                player_->restart(chart::kIdle);
            }
            return;
        }
    }
    wakeInTown();
    showMessage("No save yet: you wake up in town");
}

bool Game::gameOverEvent(const SDL_Event& event) {
    if (!fallTicks_) return false;
    if (event.type == SDL_QUIT) {
        quit_ = true;
        return true;
    }
    if (!gameOverOpen_) return true;  // the hero is falling: nothing to do yet
    switch (event.type) {
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_UP:
                case SDLK_DOWN: gameOverChoice_ = 1 - gameOverChoice_; break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE: chooseGameOver(gameOverChoice_ == 0); break;
                default: break;
            }
            break;
        case SDL_MOUSEMOTION:
            if (on(kContinue, event.motion.x, event.motion.y)) gameOverChoice_ = 0;
            if (on(kExit, event.motion.x, event.motion.y)) gameOverChoice_ = 1;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button != SDL_BUTTON_LEFT) break;
            if (on(kContinue, event.button.x, event.button.y)) chooseGameOver(true);
            else if (on(kExit, event.button.x, event.button.y)) chooseGameOver(false);
            break;
        default: break;
    }
    return true;
}

void Game::renderGameOver() {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 150);
    SDL_Rect all{0, 0, kScreenWidth, kScreenHeight};
    SDL_RenderFillRect(renderer_, &all);
    box(renderer_, kPanel, {14, 12, 18, 235}, {150, 130, 90, 255});

    const char* title = "GAME OVER";
    drawText(renderer_, kPanel.x + (kPanel.w - textWidth(title, 3)) / 2, kPanel.y + 22, title, kTitle, 3);

    const bool saved = heroHasSave();
    struct Button {
        const SDL_Rect& rect;
        const char* label;
        const char* note;
    };
    const Button buttons[] = {
        {kContinue, "CONTINUE", saved ? "FROM YOUR LAST SAVE" : "NO SAVE YET: WAKE UP IN TOWN"},
        {kExit, "EXIT", "BACK TO THE TITLE SCREEN"},
    };
    for (int i = 0; i < 2; ++i) {
        const Button& b = buttons[i];
        const bool lit = i == gameOverChoice_;
        if (lit) box(renderer_, b.rect, {60, 45, 20, 200}, {150, 130, 90, 255});
        uiText(b.rect.x + (b.rect.w - uiTextWidth(b.label)) / 2, b.rect.y + 3, b.label, lit ? kLit : kLabel);
        drawText(renderer_, b.rect.x + (b.rect.w - textWidth(b.note, 1)) / 2, b.rect.y + 19, b.note, kNote, 1);
    }
}

}  // namespace sf
