// The title menu, shown when the game starts and after Save and Return to
// Title. Esc during play opens the Settings Menu instead (options.cpp).
#include <filesystem>

#include "core/log.h"
#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {
namespace {

const SDL_Color kTitleColor{255, 210, 80, 255};
const SDL_Color kChosen{255, 255, 255, 255};
const SDL_Color kOther{150, 150, 150, 255};
const SDL_Color kDisabled{70, 70, 70, 255};

constexpr int kItemScale = 3;
constexpr int kItemGap = 28;
constexpr int kFirstItemY = 230;

const char* label(Game::MenuItem item) {
    switch (item) {
        case Game::MenuItem::Continue: return "CONTINUE";
        case Game::MenuItem::NewGame: return "NEW GAME";
        case Game::MenuItem::Options: return "OPTIONS";
        case Game::MenuItem::Quit: return "QUIT";
    }
    return "";
}

}  // namespace

void Game::openMenu() {
    menuOpen_ = true;
    optionsOpen_ = false;
    helpOpen_ = false;
    using M = MenuItem;
    menuItems_ = {M::Continue, M::NewGame, M::Options, M::Quit};
    menuChoice_ = 0;
    // Start on the first thing that can be chosen (New Game without a save).
    while (!menuItemEnabled(menuItems_[menuChoice_])) ++menuChoice_;
    mouseHeld_ = false;
    walkTarget_.reset();
    attackTarget_.reset();
}

bool Game::menuItemEnabled(MenuItem item) const {
    if (item == MenuItem::Continue) {
        std::error_code ec;
        return std::filesystem::exists(saveFile_, ec);
    }
    return true;
}

void Game::chooseMenuItem(MenuItem item) {
    if (!menuItemEnabled(item)) return;
    switch (item) {
        case MenuItem::Continue:
            if (loadGame()) menuOpen_ = false;
            break;
        case MenuItem::NewGame:
            // The Create New Character screen first, as in the original.
            menuOpen_ = false;
            openNewCharacter();
            break;
        case MenuItem::Options: openOptions(false); break;
        case MenuItem::Quit: quit_ = true; break;
    }
}

void Game::newGame() {
    hand_.reset();
    inventory_.clear();
    flags_.clear();
    explored_.clear();
    mapOpen_ = false;
    playerLevel_ = 1;
    career_ = {};
    showStatus_ = false;
    closeMagicWindow();
    power_ = {};
    shops_.clear();
    openShop_.reset();
    dialogue_.reset();
    playerExperience_ = 0;
    spells_ = {};
    spellOn_ = {};
    casting_.reset();
    clearSpells();
    setOriginalMode(settings_.originalMode);
    partnerRecord_ = {};
    partner_.reset();
    partnerHpCarried_ = 0;
    giveStartingKit();
    updateEquipment();
    if (!mapNames_.empty()) goToStart();
    playerHp_ = playerMaxHp();
    playerMp_ = playerMaxMp();
    updateCamera();
    log::info("New game");
}

// Which menu line is under a point on the screen, or -1.
int Game::menuItemAt(int x, int y) const {
    for (std::size_t i = 0; i < menuItems_.size(); ++i) {
        const int w = textWidth(label(menuItems_[i]), kItemScale);
        const int top = kFirstItemY + static_cast<int>(i) * kItemGap;
        if (x >= (kScreenWidth - w) / 2 - 8 && x <= (kScreenWidth + w) / 2 + 8 && y >= top - 6 &&
            y <= top + textHeight(kItemScale) + 6) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Game::menuEvent(const SDL_Event& event) {
    const int count = static_cast<int>(menuItems_.size());
    auto step = [&](int by) {
        for (int i = 0; i < count; ++i) {
            menuChoice_ = (menuChoice_ + by + count) % count;
            if (menuItemEnabled(menuItems_[menuChoice_])) break;
        }
    };
    switch (event.type) {
        case SDL_QUIT: quit_ = true; break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_UP:
                case SDLK_w: step(-1); break;
                case SDLK_DOWN:
                case SDLK_s: step(1); break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER:
                case SDLK_SPACE: chooseMenuItem(menuItems_[menuChoice_]); break;
                default: break;
            }
            break;
        case SDL_MOUSEMOTION:
            if (const int i = menuItemAt(event.motion.x, event.motion.y); i >= 0 && menuItemEnabled(menuItems_[i])) {
                menuChoice_ = i;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button != SDL_BUTTON_LEFT) break;
            if (const int i = menuItemAt(event.button.x, event.button.y); i >= 0) chooseMenuItem(menuItems_[i]);
            break;
        default: break;
    }
}

void Game::renderMenu() {
    // The world stays behind the menu, darkened.
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 170);
    SDL_Rect all{0, 0, kScreenWidth, kScreenHeight};
    SDL_RenderFillRect(renderer_, &all);

    const char* title = "SHADOWFLARE";
    drawText(renderer_, (kScreenWidth - textWidth(title, 6)) / 2, 110, title, kTitleColor, 6);
    const char* subtitle = "OPEN EDITION";
    drawText(renderer_, (kScreenWidth - textWidth(subtitle, 2)) / 2, 160, subtitle, kOther, 2);

    for (std::size_t i = 0; i < menuItems_.size(); ++i) {
        const auto item = menuItems_[i];
        const bool chosen = static_cast<int>(i) == menuChoice_;
        const SDL_Color color = !menuItemEnabled(item) ? kDisabled : chosen ? kChosen : kOther;
        const char* text = label(item);
        const int x = (kScreenWidth - textWidth(text, kItemScale)) / 2, y = kFirstItemY + static_cast<int>(i) * kItemGap;
        if (chosen) {
            // A small gold marker on each side of the chosen line.
            SDL_SetRenderDrawColor(renderer_, kTitleColor.r, kTitleColor.g, kTitleColor.b, 255);
            SDL_Rect left{x - 18, y + 5, 6, 6}, right{x + textWidth(text, kItemScale) + 12, y + 5, 6, 6};
            SDL_RenderFillRect(renderer_, &left);
            SDL_RenderFillRect(renderer_, &right);
        }
        drawText(renderer_, x, y, text, color, kItemScale);
    }
    const char* help = "ARROWS OR MOUSE TO CHOOSE, ENTER TO START";
    drawText(renderer_, (kScreenWidth - textWidth(help, 1)) / 2, kScreenHeight - 24, help, kOther, 1);
}

}  // namespace sf
