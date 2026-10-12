// The status window (S): the hero's class, level and stats, and the Change
// Career button that takes a class on offer (see Career in
// logic/progression.h).
#include <format>
#include <string>

#include "core/log.h"
#include "game/game.h"
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
    if (y < 37 && x >= 160) toggleMagicWindow();
    return true;
}

void Game::changeCareer() {
    if (!career_.change()) return;
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
    // The original's window has the labels drawn in; without it, plain
    // boxes and our own labels stand in.
    const SpriteBank* bank = itemBank("System/Game/Pattern/Status");
    const SpriteTexture* window = bank ? sprites_.get(*bank, 5, false) : nullptr;
    if (window) {
        const SDL_Rect dst{window->originX, window->originY, window->width, window->height};
        SDL_RenderCopy(renderer_, window->texture, nullptr, &dst);
    } else {
        frame(renderer_, kStatusPanel, {20, 18, 26, 225}, {150, 130, 90, 255});
        drawText(renderer_, 16, 16, "STATUS", kGold);
    }

    const HeroStats stats = playerStats();
    std::string values[kFieldCount];
    values[kLevel] = std::format("{}", playerLevel_);
    values[kExperience] = std::format("{}", playerExperience_);
    values[kHp] = std::format("{}/{}", playerHp_, stats[HeroStats::kHp]);
    values[kStrength] = std::format("{}", stats[HeroStats::kStrength]);
    values[kAttack] = std::format("{}", stats[HeroStats::kAttack]);
    values[kDefense] = std::format("{}", stats[HeroStats::kDefense]);
    values[kHit] = std::format("{}", stats[HeroStats::kHit]);
    values[kEvasion] = std::format("{}", stats[HeroStats::kEvasion]);
    values[kWalkSpeed] = std::format("{}", stats[HeroStats::kWalkSpeed]);
    values[kAttackSpeed] = std::format("{}", stats[HeroStats::kAttackSpeed]);
    values[kMp] = std::format("{}/{}", playerMp_, stats[HeroStats::kMp]);
    values[kMagicAttack] = std::format("{}", stats[HeroStats::kMagicAttack]);
    values[kMagicDefense] = std::format("{}", stats[HeroStats::kMagicDefense]);
    values[kMagicHit] = std::format("{}", stats[HeroStats::kMagicHit]);
    values[kMagicEvasion] = std::format("{}", stats[HeroStats::kMagicEvasion]);
    for (int f = 0; f < kFieldCount; ++f) {
        const SDL_Rect& r = kFields[f].box;
        if (!window) {
            frame(renderer_, r, {45, 40, 50, 255}, {110, 100, 80, 255});
            drawText(renderer_, r.x + 3, r.y + 3, kFields[f].label, kWhite, 1);
        }
        if (f == kClass) continue;
        drawText(renderer_, r.x + r.w - 4 - textWidth(values[f]), r.y + 3, values[f], kWhite);
    }
    drawText(renderer_, kFields[kClass].box.x + 2, kFields[kClass].box.y + 3, className(career_.current, heroSex_), kWhite);
    // The hero's name, in the long box beside the class (where it goes is
    // inferred from the window's layout).
    constexpr SDL_Rect kNameBox{88, 40, 136, 16};
    if (!window && !heroName_.empty()) frame(renderer_, kNameBox, {45, 40, 50, 255}, {110, 100, 80, 255});
    uiText(kNameBox.x + 4, kNameBox.y + 2, heroName_, kWhite);

    // The button only shows while another class is on offer.
    if (career_.canChange()) {
        const bool hover = inside(kChangeButton, mx, my);
        frame(renderer_, kChangeButton, hover ? SDL_Color{80, 80, 160, 255} : SDL_Color{50, 50, 110, 255}, {120, 120, 190, 255});
        const std::string text = std::format("CHANGE CAREER: {}", className(career_.offered, heroSex_));
        drawText(renderer_, kChangeButton.x + (kChangeButton.w - textWidth(text, 1)) / 2, kChangeButton.y + 6, text,
                 hover ? kGold : kWhite, 1);
    }
}

}  // namespace sf
