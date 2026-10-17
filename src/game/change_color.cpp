// The Change Color window, which CHANGECOLOR opens (ShadowFlare.exe
// 0x434254): the hero dyes the weapon, shield and body armour they wear
// one of sixteen colours, seeing it on a picture of themselves they can
// turn round. 0x40f9e0 draws it and takes its clicks, at the places used
// here; OK keeps the colours, Cancel (or a right click) puts back the ones
// it opened with (0x410360). See docs/change_color.md.
#include <algorithm>

#include "core/log.h"
#include "game/game.h"
#include "render/character_draw.h"

namespace sf {

namespace {

// The tabs: the weapon, the shield and the body armour (player + 0x4f4,
// + 0x4f8 and + 0x4ec).
constexpr std::array<EquipSlot, 3> kTabSlots{EquipSlot::Weapon, EquipSlot::Shield, EquipSlot::Body};
// The tabs' buttons take clicks strictly inside x 0xad-0xd6 and these.
constexpr int kTabLeft = 0xad, kTabRight = 0xd6;
constexpr std::array<std::pair<int, int>, 3> kTabRows{{{0xc6, 0xd7}, {0xde, 0xef}, {0xf6, 0x107}}};
// The 4 x 4 dyes: swatch (col, row) at (0x14c + 32 col, 0xbb + 24 row);
// a click counts from x 0x14c to 0x165 and y 0xbb to 0xcc of it.
constexpr int kSwatchX = 0x14c, kSwatchY = 0xbb, kSwatchW = 0x19, kSwatchH = 0x11, kSwatchStepX = 32, kSwatchStepY = 24;
// The picture of the hero stands at (0x10d, 0xf4); a 3 x 3 grid of 33 x 31
// cells over it from (0xd8, 0xb4) turns them to face that way (the middle
// does nothing).
constexpr int kHeroX = 0x10d, kHeroY = 0xf4;
constexpr int kPadX = 0xd8, kPadY = 0xb4, kPadW = 0x21, kPadH = 0x1f;
constexpr std::array<int, 9> kPadDirections{5, 4, 3, 6, -1, 2, 7, 0, 1};
// Restore, OK and Cancel, strictly inside.
constexpr SDL_Rect kRestore{399, 0x117, 0x1d0 - 399, 0x128 - 0x117};
constexpr SDL_Rect kOk{0xc5, 299, 0xf1 - 0xc5, 0x13d - 299};
constexpr SDL_Rect kCancel{0x14e, 299, 0x1c4 - 0x14e, 0x13d - 299};
// Clicks above the panel belong to the window.
constexpr int kClickBottom = 0x19c;

bool strictlyInside(const SDL_Rect& r, int x, int y) { return x > r.x && x < r.x + r.w && y > r.y && y < r.y + r.h; }

// Status.njp's pictures, or our pack's ChangeColor sheet, which lists them
// in this order.
struct Parts {
    int window, restore, okLit, cancelLit;
    std::array<int, 3> tabs;
    int swatch;
};
constexpr Parts kOriginalParts{0x66, 0x69, 0x6a, 0x68, {0x6c, 0x6b, 0x67}, 0x6d};
constexpr Parts kOpenParts{0, 1, 2, 3, {4, 5, 6}, 7};

}  // namespace

void Game::openChangeColor() {
    if (!player_) return;
    // Every other window closes (0x4170f0).
    if (showInventory_) closeInventory();
    showStatus_ = false;
    closeMissions();
    closeAreaMap();
    closeMagicWindow();
    closeWarpWindow();
    helpOpen_ = false;
    ChangeColor c;
    c.direction = player_->direction;
    for (std::size_t t = 0; t < kTabSlots.size(); ++t) {
        if (const auto& worn = inventory_.equipped(kTabSlots[t])) c.saved[t] = worn->colour;
    }
    changeColor_ = c;
    log::info("The Change Color window opens");
}

void Game::closeChangeColor(bool keep) {
    if (!changeColor_) return;
    if (!keep) {
        for (std::size_t t = 0; t < kTabSlots.size(); ++t) inventory_.setColour(kTabSlots[t], changeColor_->saved[t]);
        updateEquipment();
    }
    changeColor_.reset();
}

bool Game::changeColorEvent(const SDL_Event& event) {
    if (!changeColor_) return false;
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        closeChangeColor(false);
        return true;
    }
    if (event.type != SDL_MOUSEBUTTONDOWN) return false;
    const int x = event.button.x, y = event.button.y;
    if (y >= kClickBottom) return false;
    if (event.button.button == SDL_BUTTON_RIGHT) {
        closeChangeColor(false);  // (0x44ade0)
        return true;
    }
    if (event.button.button != SDL_BUTTON_LEFT) return true;
    auto& c = *changeColor_;
    auto has = [&](int tab) { return inventory_.equipped(kTabSlots[static_cast<std::size_t>(tab)]).has_value(); };
    if (c.tab >= 0 && has(c.tab)) {
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col) {
                const int right = kSwatchX + kSwatchW + kSwatchStepX * col, bottom = kSwatchY + kSwatchH + kSwatchStepY * row;
                if (x >= right - kSwatchW && x <= right && y >= bottom - kSwatchH && y <= bottom) {
                    inventory_.setColour(kTabSlots[static_cast<std::size_t>(c.tab)], col + 4 * row);
                    updateEquipment();
                }
            }
        }
    }
    if (x > kTabLeft && x < kTabRight) {
        for (int t = 0; t < 3; ++t) {
            const auto [top, bottom] = kTabRows[static_cast<std::size_t>(t)];
            if (y > top && y < bottom && has(t)) {
                c.tab = t;
                playSound(0x3a);
            }
        }
    }
    if (x >= kPadX && x < kPadX + 3 * kPadW && y >= kPadY && y < kPadY + 3 * kPadH) {
        const int d = kPadDirections[static_cast<std::size_t>((y - kPadY) / kPadH * 3 + (x - kPadX) / kPadW)];
        if (d >= 0) c.direction = d;
    }
    if (strictlyInside(kRestore, x, y) && c.tab >= 0 && has(c.tab)) {
        inventory_.setColour(kTabSlots[static_cast<std::size_t>(c.tab)], -1);
        updateEquipment();
    }
    if (strictlyInside(kOk, x, y)) {
        closeChangeColor(true);
        playSound(0x3a);
    } else if (strictlyInside(kCancel, x, y)) {
        closeChangeColor(false);
        playSound(0x3a);
    }
    return true;
}

void Game::renderChangeColor() {
    if (!changeColor_ || !player_) return;
    auto& c = *changeColor_;
    // The tab moves on past items not worn, as the window draws (0x40f9e0).
    auto has = [&](int tab) { return inventory_.equipped(kTabSlots[static_cast<std::size_t>(tab)]).has_value(); };
    if (c.tab == -1) {
        for (int t = 0; t < 3 && c.tab == -1; ++t) {
            if (has(t)) c.tab = t;
        }
    }
    for (int t = 0; t < 3; ++t) {
        if (c.tab == t && !has(t)) c.tab = t + 1;
    }
    if (c.tab == 3) c.tab = -1;

    int mx = 0, my = 0;
    {
        int wx, wy;
        SDL_GetMouseState(&wx, &wy);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
        mx = static_cast<int>(lx);
        my = static_cast<int>(ly);
    }
    const SpriteBank* bank = itemBank("System/Game/Pattern/Status");
    Parts parts = kOriginalParts;
    if (!bank || bank->patterns.size() <= static_cast<std::size_t>(kOriginalParts.swatch)) {
        bank = itemBank("System/Game/Pattern/ChangeColor");
        parts = kOpenParts;
        if (bank && bank->patterns.size() <= static_cast<std::size_t>(kOpenParts.swatch)) bank = nullptr;
    }
    auto draw = [&](int pattern, int x = 0, int y = 0, SDL_Color tint = {255, 255, 255, 255}) {
        const SpriteTexture* t = bank ? sprites_.get(*bank, pattern, false) : nullptr;
        if (!t) return false;
        const SDL_Rect dst{x + t->originX, y + t->originY, t->width, t->height};
        SDL_SetTextureColorMod(t->texture, tint.r, tint.g, tint.b);
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        SDL_SetTextureColorMod(t->texture, 255, 255, 255);
        return true;
    };

    if (!draw(parts.window)) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 30, 30, 34, 235);
        const SDL_Rect r{160, 144, 320, 192};
        SDL_RenderFillRect(renderer_, &r);
        uiText(176, 150, "Change Color", {224, 224, 224, 255});
    }
    draw(parts.restore);
    if (strictlyInside(kOk, mx, my)) draw(parts.okLit);
    if (strictlyInside(kCancel, mx, my)) draw(parts.cancelLit);

    if (c.tab >= 0) {
        draw(parts.tabs[static_cast<std::size_t>(c.tab)]);
        for (int i = 0; i < 16; ++i) {
            const auto& dye = kDyes[static_cast<std::size_t>(i)];
            auto channel = [](int permille) { return static_cast<Uint8>(std::clamp(permille, 0, 1000) * 255 / 1000); };
            const int col = i % 4, row = i / 4;
            if (!draw(parts.swatch, kSwatchStepX * col, kSwatchStepY * row, {channel(dye[0]), channel(dye[1]), channel(dye[2]), 255})) {
                SDL_SetRenderDrawColor(renderer_, channel(dye[0]), channel(dye[1]), channel(dye[2]), 255);
                const SDL_Rect r{kSwatchX + kSwatchStepX * col + 2, kSwatchY + kSwatchStepY * row + 2, 22, 14};
                SDL_RenderFillRect(renderer_, &r);
            }
        }
        // A pale box round the chosen dye.
        const int chosen = inventory_.equipped(kTabSlots[static_cast<std::size_t>(c.tab)])->colour;
        if (chosen >= 0) {
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 128);
            const SDL_Rect r{kSwatchX + kSwatchStepX * (chosen % 4), kSwatchY + kSwatchStepY * (chosen / 4), kSwatchW + 1, kSwatchH + 1};
            SDL_RenderDrawRect(renderer_, &r);
        }
    }

    // The hero as they are dressed, in the fighting stance of an axe or a
    // great sword when they hold one.
    Character hero = *player_;
    int chartNumber = chart::kIdle;
    if (const auto& weapon = inventory_.equipped(EquipSlot::Weapon)) {
        if (weapon->def->subtype == ItemDef::kAxe) chartNumber = 0x18;
        if (weapon->def->subtype == ItemDef::kGreatSword) chartNumber = 0x19;
    }
    if (!hero.hasChart(chartNumber)) chartNumber = chart::kIdle;
    hero.direction = c.direction;
    hero.restart(chartNumber);
    // Its frame counts the original's ticks, 30 a second.
    if (hero.frameCount() > 0) hero.setFrame(static_cast<int>(ticks_ * 30 / kTicksPerSecond) % hero.frameCount());
    hero.x = hero.y = 0;
    drawCharacter(renderer_, sprites_, hero, -kHeroX, -kHeroY);
}

}  // namespace sf
