// The game panel along the bottom of the screen, as ShadowFlare.exe draws
// it (0x4039f0) from System/Game/Pattern/Bar.njp, whose patterns carry
// their own screen positions:
//
//   0, 1, 2   HP gauge: filled, gaining (bright), losing (dark), at (81, 426)
//   3         MP gauge, at (106, 453); 4 and 5 are its unused bright and dark
//   6         one pixel: the glints that run along the gauges
//   7, 8      the panel itself: its top edge at y 400, the rest from 412
//   9         MINE button lit, with a mine in hand
//   10, 11    WALK or RUN button dimmed while the other is the default
//   12, 13    the increased-power button blinking, and pressed
//   14, 15    EXP gauge and its frame, top right
//   16-18     ITEM, MENU and STATUS lit for a frame when clicked
//   19-28     the level's digits 0-9
//   29-32     the companion's gauge (partner.cpp)
//
// The open content pack has its own Bar sheet with the same numbering
// (tools/art/panel.py). With neither, plain bars stand in.

#include <algorithm>
#include <format>

#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr int kHpX = 81, kHpTop = 425, kHpBottom = 436;  // 0x51, 0x1a9, 0x1b4
constexpr int kMpX = 106, kMpTop = 452, kMpBottom = 463;  // 0x6a, 0x1c4, 0x1cf
constexpr int kGaugeWidth = 206;                           // 0xce
constexpr int kExpX = 530, kExpTop = 395, kExpBottom = 404, kExpWidth = 109;
constexpr int kLevelY = 477;  // 0x1dd: the digits' anchor, 8 right and 12 below their corner

// Where the clicks land (0x445d10): x0, y0, x1, y1, all inclusive.
struct Area {
    int x0, y0, x1, y1;
    bool has(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
};
constexpr Area kMenuButton{589, 402, 639, 413};
constexpr Area kRunButton{360, 448, 390, 457};
constexpr Area kWalkButton{368, 464, 398, 473};
constexpr Area kStatusButton{537, 420, 577, 437};
constexpr Area kItemButton{583, 429, 636, 448};
constexpr int kPanelTop = 412;  // every click below this is the panel's

enum Flash { kFlashStatus, kFlashItem, kFlashMenu };

}  // namespace

bool Game::hasPanelArt() {
    const SpriteBank* bar = itemBank("System/Game/Pattern/Bar");
    return bar && bar->patterns.size() > 28;
}

bool Game::panelClick(int x, int y) {
    if (!player_ || menuOpen_) return false;
    if (showInventory_ && x >= 320 && y < 416) return false;  // the item window covers the panel's top right
    if (kMenuButton.has(x, y)) {
        panel_.flash[kFlashMenu] = 2;
        openOptions(true);
        return true;
    }
    if (y < kPanelTop) return false;
    if (kRunButton.has(x, y)) {
        runMode_ = true;
    } else if (kWalkButton.has(x, y)) {
        runMode_ = false;
    } else if (kStatusButton.has(x, y) && !openShop_) {
        showStatus_ = !showStatus_;
        if (showStatus_) {
            closeMissions();
            panel_.flash[kFlashStatus] = 2;
        }
    } else if (kItemButton.has(x, y) && !openShop_) {
        closeMissions();
        if (showInventory_) {
            closeInventory();
        } else {
            showInventory_ = true;
            panel_.flash[kFlashItem] = 2;
        }
    }
    // The rest of the panel only stops the hero walking under it.
    return true;
}

void Game::renderPanel() {
    if (!player_ || menuOpen_) return;
    const SpriteBank* bar = hasPanelArt() ? itemBank("System/Game/Pattern/Bar") : nullptr;

    // The original's frames, 30 a second, that have passed since last time.
    const std::uint64_t now = ticks_ * 3 / 2;
    const int frames = static_cast<int>(std::min<std::uint64_t>(now - std::min(now, panel_.frame), 4));
    panel_.frame = now;
    const bool blinkOn = (now % 4) < 2;  // +0xa0a4 & 3 < 2

    const int maxHp = std::max(1, playerMaxHp()), maxMp = std::max(1, playerMaxMp());
    int hpWidth = playerHp_ * kGaugeWidth / maxHp;
    if (playerHp_ > 0 && hpWidth == 0) hpWidth = 1;
    int mpWidth = playerMp_ * kGaugeWidth / maxMp;
    if (playerMp_ > 0 && mpWidth == 0) mpWidth = 1;

    if (!bar) {
        auto plain = [&](int x, int y, int w, int h, int fill, SDL_Color color) {
            SDL_Rect back{x, y, w, h};
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            SDL_RenderFillRect(renderer_, &back);
            SDL_Rect inner{x + 1, y + 1, std::clamp(fill, 0, w - 2), h - 2};
            SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, 255);
            SDL_RenderFillRect(renderer_, &inner);
        };
        plain(kHpX, kHpTop, kGaugeWidth + 2, 10, hpWidth, {200, 30, 30, 255});
        plain(kMpX, kMpTop, kGaugeWidth + 2, 10, mpWidth, {40, 70, 210, 255});
        const auto need = levels_.need(playerLevel_);
        const int exp = need > 0 ? static_cast<int>(std::min<std::int64_t>(playerExperience_ * kExpWidth / need, kExpWidth)) : 0;
        plain(kExpX, kExpTop, kExpWidth + 2, 6, exp, {70, 120, 230, 255});
        drawText(renderer_, 8, kLevelY - 12, std::format("LV {}", playerLevel_), {255, 255, 255, 255});
        return;
    }

    auto texture = [&](int n) { return sprites_.get(*bar, n, false); };
    // A pattern where it belongs, cut to `clip` (screen pixels) if given.
    auto draw = [&](int n, const SDL_Rect* clip = nullptr) {
        const SpriteTexture* t = texture(n);
        if (!t) return;
        SDL_Rect dst{t->originX, t->originY, t->width, t->height};
        SDL_Rect src{0, 0, t->width, t->height};
        if (clip) {
            SDL_Rect cut;
            if (!SDL_IntersectRect(&dst, clip, &cut)) return;
            src = {cut.x - dst.x, cut.y - dst.y, cut.w, cut.h};
            dst = cut;
        }
        SDL_RenderCopy(renderer_, t->texture, &src, &dst);
    };
    // Brightness 1500 on the exe's scale of 1000: the picture again, added at half strength.
    auto brighten = [&](int n, const SDL_Rect* clip) {
        const SpriteTexture* t = texture(n);
        if (!t) return;
        SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(t->texture, 128);
        draw(n, clip);
        SDL_SetTextureAlphaMod(t->texture, 255);
        SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_BLEND);
    };
    auto flash = [&](int n) {  // the exe's alpha 300
        const SpriteTexture* t = texture(n);
        if (!t) return;
        SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(t->texture, 77);
        draw(n);
        SDL_SetTextureAlphaMod(t->texture, 255);
        SDL_SetTextureBlendMode(t->texture, SDL_BLENDMODE_BLEND);
    };

    draw(7);
    draw(8);
    static constexpr int kFlashPattern[3] = {18, 16, 17};
    for (int k = 0; k < 3; ++k) {
        if (panel_.flash[k] <= 0) continue;
        flash(kFlashPattern[k]);
        panel_.flash[k] = std::max(0, panel_.flash[k] - frames);
    }
    draw(runMode_ ? 10 : 11);

    // The level, its digits popping to 1.5 times their size one after
    // another when it changes: ones, tens, then hundreds.
    if (panel_.levelCounter == -1) panel_.levelCounter = playerLevel_;
    for (int f = 0; f < frames && panel_.levelCounter != playerLevel_; ++f) {
        if (panel_.levelCounter < 10000) {
            panel_.levelCounter = 10000;
        } else if (panel_.levelCounter < 10008) {
            ++panel_.levelCounter;
        } else {
            panel_.levelCounter = playerLevel_;
        }
    }
    auto pop = [&](int first) {  // 1.5 on the counter's first frame for this digit, then shrinking
        const int c = panel_.levelCounter;
        if (c == first) return 1500;
        if (c > first && c < first + 4) return 1500 - (c - first) * 166;
        return 1000;
    };
    static constexpr int kDigitX[3][3] = {{0, 0, 60}, {0, 56, 65}, {51, 60, 69}};
    const int digits = playerLevel_ > 99 ? 2 : playerLevel_ > 9 ? 1 : 0;
    auto digit = [&](int x, int value, int scale) {
        const SpriteTexture* t = texture(19 + value);
        if (!t) return;
        const SDL_Rect dst{x + 8 - 8 * scale / 1000, kLevelY - 12 * scale / 1000, t->width * scale / 1000, t->height * scale / 1000};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
    };
    digit(kDigitX[digits][2], playerLevel_ % 10, pop(10000));
    if (playerLevel_ > 9) digit(kDigitX[digits][1], playerLevel_ / 10 % 10, pop(10004));
    if (playerLevel_ > 99) digit(kDigitX[digits][0], playerLevel_ / 100 % 10, pop(10008));

    // HP: the gauge follows the real value 2 pixels a frame; what is being
    // lost shows dark, what is being gained bright.
    for (int f = 0; f < frames; ++f) {
        if (panel_.hpShown > hpWidth) panel_.hpShown = std::max(hpWidth, panel_.hpShown - 2);
        else if (panel_.hpShown < hpWidth) panel_.hpShown = std::min(hpWidth, panel_.hpShown + 2);
    }
    if (panel_.hpShown > hpWidth) {
        const SDL_Rect lost{kHpX + hpWidth, kHpTop, panel_.hpShown - hpWidth, kHpBottom - kHpTop};
        draw(2, &lost);
    }
    const SDL_Rect hp{kHpX, kHpTop, hpWidth - 1, kHpBottom - kHpTop};  // the exe's rectangles stop a pixel short
    draw(0, &hp);
    if (panel_.hpShown < hpWidth) {
        const SDL_Rect gained{kHpX + panel_.hpShown, kHpTop, hpWidth - panel_.hpShown, kHpBottom - kHpTop};
        draw(1, &gained);
    }

    // Glints: specks of light that speed along the gauge's glass and go
    // out at its end; each of 60 slots starts one now and then.
    auto glints = [&](auto& slots, int width, int x0, int y0) {
        auto random = [&] {  // the C library's rand(), on our own seed
            panel_.random = panel_.random * 214013u + 2531011u;
            return static_cast<int>((panel_.random >> 16) & 0x7fff);
        };
        const SpriteTexture* speck = texture(6);
        for (auto& g : slots) {
            for (int f = 0; f < frames && g.age == 0; ++f) {
                if ((random() & 0x7f) / 120 == 1) {
                    g.age = 1;
                    g.speed = (random() & 7) + 5;
                    g.dy = random() & 7;
                }
            }
            if (g.age == 0) continue;
            const int x = g.age * g.age * g.speed / 200;
            if (x >= width - 3) {
                g.age = 0;
                continue;
            }
            if (speck) {
                const int size = (g.speed & 1) ? 2 : 1;
                const int alpha = ((g.speed & 1) ? 15 : 22) * g.speed;  // of 1000
                SDL_SetTextureBlendMode(speck->texture, SDL_BLENDMODE_ADD);
                SDL_SetTextureAlphaMod(speck->texture, static_cast<Uint8>(std::min(255, alpha * 255 / 1000)));
                const SDL_Rect dst{x + x0, y0 + (random() & 1) + g.dy, size, size};
                SDL_RenderCopy(renderer_, speck->texture, nullptr, &dst);
                SDL_SetTextureAlphaMod(speck->texture, 255);
                SDL_SetTextureBlendMode(speck->texture, SDL_BLENDMODE_BLEND);
            }
            g.age += frames;
        }
    };
    glints(panel_.glints[0], hpWidth, kHpX + 2, kHpTop + 2);

    // MP follows 4 pixels a frame, with no dark or bright part.
    for (int f = 0; f < frames; ++f) {
        if (panel_.mpShown > mpWidth) panel_.mpShown = std::max(mpWidth, panel_.mpShown - 4);
        else if (panel_.mpShown < mpWidth) panel_.mpShown = std::min(mpWidth, panel_.mpShown + 4);
    }
    const SDL_Rect mp{kMpX, kMpTop, panel_.mpShown - 1, kMpBottom - kMpTop};
    draw(3, &mp);
    glints(panel_.glints[1], mpWidth, kMpX + 2, kMpTop + 2);

    if (mineCount_ > 0 && mineWait_ == 0) draw(9);

    // EXP: the frame, and the gauge, blinking bright from 90% full; empty at the top level.
    draw(15);
    const auto need = levels_.need(playerLevel_);
    if (need > 0) {
        const int exp = playerExperience_ >= need ? kExpWidth : static_cast<int>(playerExperience_ * kExpWidth / need);
        if (exp > 0) {
            const SDL_Rect gauge{kExpX, kExpTop, exp - 1, kExpBottom - kExpTop};
            draw(14, &gauge);
            if (exp * 100 / kExpWidth > 89 && blinkOn) brighten(14, &gauge);
        }
    }
}

}  // namespace sf
