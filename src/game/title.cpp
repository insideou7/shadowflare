// The title menu, shown when the game starts and after Save and Return to
// Title. Esc during play opens the Settings Menu instead (options.cpp).
#include <algorithm>
#include <filesystem>
#include <format>

#include "core/log.h"
#include "game/game.h"
#include "game/skin.h"
#include "platform/filesystem.h"
#include "render/tiny_font.h"

namespace sf {
namespace {

const SDL_Color kTitleColor{255, 210, 80, 255};
const SDL_Color kChosen{255, 255, 255, 255};
const SDL_Color kOther{150, 150, 150, 255};
const SDL_Color kDisabled{70, 70, 70, 255};

constexpr int kItemScale = 3;
constexpr int kItemGap = 28;
// Below the name on the title picture, where the original has its menu.
constexpr int kFirstItemY = 300;

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
    adoptOldSave();
    menuOpen_ = true;
    optionsOpen_ = false;
    helpOpen_ = false;
    using M = MenuItem;
    menuItems_ = {M::Continue, M::NewGame, M::Options, M::Quit};
    menuChoice_ = 0;
    titleSmoke_.emplace(titleRandom_);
    titleSmokeCarry_ = 0;
    // Start on the first thing that can be chosen (New Game without a save).
    while (!menuItemEnabled(menuItems_[menuChoice_])) ++menuChoice_;
    mouseHeld_ = false;
    walkTarget_.reset();
    attackTarget_.reset();
}

bool Game::menuItemEnabled(MenuItem item) const {
    if (saveFolder_.empty()) {
        // One save file named on the command line.
        std::error_code ec;
        return item != MenuItem::Continue || std::filesystem::exists(saveFile_, ec);
    }
    // As the original's title: Load Game while there is a save, New Game
    // while one of the six slots is free (the manual's Starting the Game).
    if (item == MenuItem::Continue) return !savedSlots().empty();
    if (item == MenuItem::NewGame) return freeSlot().has_value();
    return true;
}

void Game::chooseMenuItem(MenuItem item) {
    if (!menuItemEnabled(item)) return;
    switch (item) {
        case MenuItem::Continue:
            if (saveFolder_.empty()) {
                if (loadGame()) menuOpen_ = false;
            } else {
                // The Load Data screen, to choose one of the saves.
                menuOpen_ = false;
                openLoadData();
            }
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
    forgetVisitedScenarios();
    hand_.reset();
    specialOpen_ = false;
    inventory_.clear();
    flags_.clear();
    // The first gate, Remote Town's, is known from the start (0x4412dc).
    flags_[{ScenarioScript::kWarp, 0}] = 1;
    // Pages I and II of the Giant Warehouse are open from the start (0x440f70).
    flags_[{ScenarioScript::kExtraStorage, 0}] = 1;
    flags_[{ScenarioScript::kExtraStorage, 1}] = 1;
    giantStorage_ = {};
    giantPage_ = 0;
    closeWarpWindow();
    explored_.clear();
    mapOpen_ = false;
    playerLevel_ = 1;
    discX_ = discY_ = 0;
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
    transportGate_.reset();
    setOriginalMode(settings_.originalMode);
    partnerRecord_ = {};
    partner_.reset();
    partnerHpCarried_ = 0;
    // A new hero gets a save slot of their own when first saved.
    if (!saveFolder_.empty()) saveFile_.clear();
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
                // The picture behind shows which art is on.
                case SDLK_F12: swapArt(); break;
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
    // The title picture, pattern 0 of System/Title/Pattern/Title: the
    // whole screen, with the name on it. The original's has gaps where its
    // smoke shows through, so black first; the smoke is added on top.
    // With our art chosen (Settings Menu or F12) it is our picture, from
    // the pack, over the original's.
    constexpr const char* kTitleBank = "System/Title/Pattern/Title";
    const SpriteBank* bank = skin_ ? skin_->sheet(kTitleBank) : nullptr;
    if (!bank) bank = itemBank(kTitleBank);
    if (const SpriteTexture* t = bank ? sprites_.get(*bank, 0, false) : nullptr) {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        SDL_RenderClear(renderer_);
        const SDL_Rect dst{t->originX, t->originY, t->width, t->height};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        renderTitleSmoke();
    } else {
        // Without one the world stays behind the menu, darkened.
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 170);
        SDL_Rect all{0, 0, kScreenWidth, kScreenHeight};
        SDL_RenderFillRect(renderer_, &all);
        const char* title = "SHADOWFLARE";
        drawText(renderer_, (kScreenWidth - textWidth(title, 6)) / 2, 110, title, kTitleColor, 6);
        const char* subtitle = "OPEN EDITION";
        drawText(renderer_, (kScreenWidth - textWidth(subtitle, 2)) / 2, 160, subtitle, kOther, 2);
    }

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

const std::array<std::optional<CharacterModel>, TitleSmoke::kJets>& Game::titleSmokeJets() {
    // With our art chosen the pack's jets go with its picture; the
    // original's would blow from pipes that are not there.
    const int set = skin_ ? 1 : 0;
    if (!titleSmokeLoaded_[set]) {
        titleSmokeLoaded_[set] = true;
        for (int i = 0; i < TitleSmoke::kJets; ++i) {
            const std::string stem = std::format("System/Title/Pattern/Smoke{:02d}", i);
            if (skin_) {
                titleSmokeModels_[set][i] = skin_->character(stem);
            } else if (data_->exists(stem + ".caf") || data_->exists(stem + ".anim")) {
                titleSmokeModels_[set][i] = CharacterModel::load(*data_, stem);
            }
        }
    }
    return titleSmokeModels_[set];
}

namespace {

// Frames in a jet's one chart, facing 8 (0x47343c GetMaxFrameCount(8)).
const AnimationSet::Direction* smokeDirection(const std::optional<CharacterModel>& model) {
    if (!model || model->animations.charts.empty()) return nullptr;
    const auto& dirs = model->animations.charts[0].directions;
    return dirs.size() > 8 ? &dirs[8] : nullptr;
}

}  // namespace

void Game::tickTitleSmoke() {
    if (!titleSmoke_) return;
    const auto& jets = titleSmokeJets();
    std::array<int, TitleSmoke::kJets> frames{};
    for (int i = 0; i < TitleSmoke::kJets; ++i) {
        if (const auto* dir = smokeDirection(jets[i])) frames[i] = dir->frameCount;
    }
    // The title loop runs once for each of the original's ticks.
    for (titleSmokeCarry_ += kOriginalTicksPerTick; titleSmokeCarry_ >= 1.0; titleSmokeCarry_ -= 1.0) {
        titleSmoke_->step(frames, titleRandom_);
    }
}

void Game::renderTitleSmoke() {
    if (!titleSmoke_) return;
    const auto& jets = titleSmokeJets();
    for (int i = 0; i < TitleSmoke::kJets; ++i) {
        const auto* dir = smokeDirection(jets[i]);
        if (!dir) continue;
        const int f = titleSmoke_->frame(i, dir->frameCount);
        if (f < 0) continue;
        const auto [x, y] = TitleSmoke::kPositions[i];
        for (const auto& layer : dir->layers) {
            if (f >= static_cast<int>(layer.size())) continue;
            const auto& cell = layer[static_cast<std::size_t>(f)];
            if (AnimationSet::isShadow(cell.status)) continue;
            const SpriteTexture* sprite = sprites_.get(jets[i]->body, cell.pattern, false);
            if (!sprite) continue;
            // The original's jets are additive (status 16): steam lightens
            // what is behind it.
            const bool additive = cell.status & AnimationSet::kAdditiveStatus;
            if (additive) SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
            SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(std::clamp<int>(cell.trans, 0, 1000) * 255 / 1000));
            const SDL_Rect dst{x + sprite->originX, y + sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
            SDL_SetTextureAlphaMod(sprite->texture, 255);
            if (additive) SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
        }
    }
}

}  // namespace sf
