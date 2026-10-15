// The status window (S): the hero's class, level and stats, and the Change
// Career button that takes a class on offer (see Career in
// logic/progression.h).
#include <format>
#include <string>

#include "core/log.h"
#include "game/game.h"
#include "logic/archery.h"
#include "logic/magic.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

// Where ShadowFlare.exe draws the window: Status.njp pattern 5 fills the left
// half of the screen, and a click between (17, 64) and (112, 79) while it is
// open changes the class (0x4474b0).
constexpr SDL_Rect kStatusPanel{0, 0, 320, 416};
constexpr SDL_Rect kChangeButton{17, 64, 96, 16};
constexpr SDL_Color kWhite{235, 230, 215, 255};
constexpr SDL_Color kGold{255, 210, 80, 255};
constexpr SDL_Color kPlainText{224, 224, 224, 255};
constexpr SDL_Color kMaxText{224, 192, 128, 255};
constexpr int kTopLevel = 100;  // the original's own number (0x405750)

bool inside(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

void frame(SDL_Renderer* renderer, const SDL_Rect& r, SDL_Color fill, SDL_Color edge) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, edge.r, edge.g, edge.b, edge.a);
    SDL_RenderDrawRect(renderer, &r);
}

// A labelled box of the window, as laid out in Status.njp pattern 5; the
// value goes at its right end.
struct Field {
    const char* label;
    SDL_Rect box;
};
enum FieldId {
    kClass, kLevel, kExperience, kHp, kStrength, kAttack, kDefense, kHit, kEvasion, kWalkSpeed, kAttackSpeed,
    kMp, kMagicAttack, kMagicDefense, kMagicHit, kMagicEvasion, kFieldCount
};
constexpr Field kFields[kFieldCount] = {
    {"", {16, 40, 64, 16}},
    {"LV", {232, 40, 72, 16}},
    {"EXPERIENCE", {176, 64, 128, 16}},
    {"HP", {16, 88, 112, 16}},
    {"STRENGTH", {16, 112, 80, 16}},
    {"ATTACK", {104, 112, 96, 16}},
    {"DEFENSE", {208, 112, 96, 16}},
    {"HIT RATE", {16, 136, 136, 16}},
    {"EVASION RATE", {168, 136, 136, 16}},
    {"WALKING SPEED", {16, 160, 136, 16}},
    {"ATTACK SPEED", {168, 160, 136, 16}},
    {"MP", {16, 192, 112, 16}},
    {"M.ATTACK", {16, 216, 136, 16}},
    {"M.DEFENSE", {168, 216, 136, 16}},
    {"M.HIT RATE", {16, 240, 136, 16}},
    {"M.EVASION RATE", {168, 240, 136, 16}},
};

}  // namespace

// A click while the window is open; true when it was used up.
bool Game::statusClick(int x, int y) {
    if (!showStatus_ || openShop_ || !inside(kStatusPanel, x, y)) return false;
    if (inside(kChangeButton, x, y)) changeCareer();
    // The MAGIC tab at the top right (0x4474b0).
    if (y < 37 && x >= 160) {
        playSound(0x3a);
        toggleMagicWindow();
    }
    return true;
}

void Game::changeCareer() {
    if (!career_.change()) return;
    if (player_) playSound(0x40, player_->x, player_->y);  // at the hero (0x4474b0)
    playerHp_ = std::min(playerHp_, playerMaxHp());
    playerMp_ = std::min(playerMp_, playerMaxMp());
    log::info("Changed class to {}", className(career_.current, heroSex_));
    showMessage(std::format("Changed your occupation to [{}]", className(career_.current, heroSex_)));
}

void Game::renderStatus() {
    if (!showStatus_ || openShop_ || !player_) return;
    int mx = 0, my = 0;
    {
        int wx, wy;
        SDL_GetMouseState(&wx, &wy);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
        mx = static_cast<int>(lx);
        my = static_cast<int>(ly);
    }
    // The original's window has the labels drawn in, and so does the open
    // pack's StatusWindow.sheet (window, then the 21 bars, then the disc
    // mark); without either, plain boxes and our own labels stand in.
    const SpriteBank* bank = itemBank("System/Game/Pattern/Status");
    int windowPattern = 5, barPattern = 0x24, dotPattern = 0x39;
    const SpriteTexture* window = bank ? sprites_.get(*bank, windowPattern, false) : nullptr;
    if (!window) {
        bank = itemBank("System/Game/Pattern/StatusWindow");
        windowPattern = 0, barPattern = 1, dotPattern = 22;
        window = bank ? sprites_.get(*bank, windowPattern, false) : nullptr;
    }
    if (window) {
        const SDL_Rect dst{window->originX, window->originY, window->width, window->height};
        SDL_RenderCopy(renderer_, window->texture, nullptr, &dst);
    } else {
        frame(renderer_, kStatusPanel, {20, 18, 26, 225}, {150, 130, 90, 255});
        drawText(renderer_, 16, 16, "STATUS", kGold);
    }

    // The numbers where ShadowFlare.exe writes them (0x405750): each ends
    // at x, tan when what is worn raises it above the hero's own, red when
    // it lowers it. HP and MP are now / most.
    const HeroStats stats = playerStats(), own = heroStats();
    struct Number {
        int x, y, value, base;
    };
    auto stat = [&](HeroStats::Stat s, int x, int y) { return Number{x, y, stats[s], own[s]}; };
    // With a bowgun, anyone but a hunter is shown the part of the attack
    // its shots carry, always in the raised tone (0x405750).
    const int bowPercent = bowInHand() ? bowAttackPercent(career_.current, career_.history) : 100;
    Number attack = stat(HeroStats::kAttack, 198, 113);
    if (bowPercent != 100) attack = {198, 113, stats[HeroStats::kAttack] * bowPercent / 100, -1};
    // At level 100 the level is tan and "Max" takes the place of the
    // experience, its black copy right under it (0x405750).
    const bool maxed = playerLevel_ >= kTopLevel;
    const Number numbers[] = {
        {303, 41, playerLevel_, maxed ? -1 : playerLevel_},
        stat(HeroStats::kHp, 124, 89),          {81, 89, playerHp_, playerHp_ - stats[HeroStats::kHp] + own[HeroStats::kHp]},
        stat(HeroStats::kStrength, 95, 113),    attack,
        stat(HeroStats::kDefense, 303, 113),    stat(HeroStats::kHit, 150, 137),
        stat(HeroStats::kEvasion, 303, 137),    stat(HeroStats::kWalkSpeed, 150, 161),
        stat(HeroStats::kAttackSpeed, 303, 161), stat(HeroStats::kMp, 124, 192),
        {81, 192, playerMp_, playerMp_ - stats[HeroStats::kMp] + own[HeroStats::kMp]},
        stat(HeroStats::kMagicAttack, 150, 217), stat(HeroStats::kMagicDefense, 303, 217),
        stat(HeroStats::kMagicHit, 150, 241),   stat(HeroStats::kMagicEvasion, 303, 241),
    };
    if (!window) {
        for (const auto& field : kFields) {
            frame(renderer_, field.box, {45, 40, 50, 255}, {110, 100, 80, 255});
            drawText(renderer_, field.box.x + 3, field.box.y + 3, field.label, kWhite, 1);
        }
    }
    for (const auto& n : numbers) windowNumber(n.x, n.y, n.value, n.base < 0 ? kRaised : toneOf(n.value, n.base));
    if (maxed) {
        windowText(283, 67, "Max", kMaxText, 0);
    } else {
        windowNumber(303, 65, static_cast<int>(playerExperience_), kPlain);
    }
    // The slashes of HP and MP; the current HP and MP take the tone of the most.
    windowText(83, 91, "/", kPlainText);
    windowText(83, 194, "/", kPlainText);
    // The class and the hero's name, in the boxes along the top.
    windowText(22, 42, className(career_.current, heroSex_), kPlainText);
    constexpr SDL_Rect kNameBox{88, 40, 136, 16};
    if (!window && !heroName_.empty()) frame(renderer_, kNameBox, {45, 40, 50, 255}, {110, 100, 80, 255});
    windowText(92, 42, heroName_, kPlainText);

    // The element levels as bars, Status.njp 0x24 (level -10) to 0x38 (+10)
    // a row of 16 pixels apart, and the hero's place on the attribute disc,
    // pattern 0x39, pulsing, 48 pixels from the middle at the edge of the
    // disc (0x405750). A new hero stands in the middle; the medicines move
    // them (see applyConsumable).
    if (window) {
        const ElementLevels levels = playerElements();
        for (int i = 0; i < kElementCount; ++i) {
            const int pattern = barPattern + std::clamp(levels[static_cast<std::size_t>(i)] + 10, 0, 20);
            if (const SpriteTexture* bar = sprites_.get(*bank, pattern, false)) {
                const SDL_Rect dst{bar->originX, bar->originY + i * 16, bar->width, bar->height};
                SDL_RenderCopy(renderer_, bar->texture, nullptr, &dst);
            }
        }
        if (const SpriteTexture* dot = sprites_.get(*bank, dotPattern, false)) {
            // Up over 25 of the original's frames and down over 25.
            const int phase = static_cast<int>(ticks_ * 30 / kTicksPerSecond % 50);
            const int alpha = phase < 25 ? phase * 255 / 25 : (50 - phase) * 255 / 25;
            SDL_SetTextureAlphaMod(dot->texture, static_cast<Uint8>(alpha));
            const SDL_Rect dst{dot->originX + 80 + discX_ * 48 / 20000, dot->originY + 330 - discY_ * 48 / 20000, dot->width,
                               dot->height};
            SDL_RenderCopy(renderer_, dot->texture, nullptr, &dst);
            SDL_SetTextureAlphaMod(dot->texture, 255);
        }
    }

    // The button only shows while another class is on offer.
    if (career_.canChange()) {
        const bool hover = inside(kChangeButton, mx, my);
        frame(renderer_, kChangeButton, hover ? SDL_Color{80, 80, 160, 255} : SDL_Color{50, 50, 110, 255}, {120, 120, 190, 255});
        const std::string text = std::format("CHANGE CAREER: {}", className(career_.offered, heroSex_));
        drawText(renderer_, kChangeButton.x + (kChangeButton.w - textWidth(text, 1)) / 2, kChangeButton.y + 6, text,
                 hover ? kGold : kWhite, 1);
    }

    // Over the scaled attack the original says by how much it is cut, in a
    // box under the pointer with a white edge (0x405750).
    if (bowPercent != 100 && mx > 101 && mx < 202 && my > 109 && my < 130) {
        const std::string text = std::format("Revision of Attack (Not for Hunters):-{}%", 100 - bowPercent);
        const int w = uiTextWidth(text) + 8, h = 18;
        const int x = std::clamp(mx - w / 2, 1, kScreenWidth - w), y = std::clamp(my + 8, 1, kScreenHeight - h);
        frame(renderer_, {x - 1, y - 1, w + 1, h + 1}, {0, 0, 0, 255}, {255, 255, 255, 255});
        uiText(x + 4, y + 3, text, kWhite);
    }
}

}  // namespace sf
