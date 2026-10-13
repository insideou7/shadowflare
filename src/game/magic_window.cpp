// The magic window (M), the STATUS window's other tab: every spell with its
// level, uses towards the next level, MP cost and power, six to a page, and
// a bar along the bottom that spells are dragged to for F1-F8. Laid out as
// ShadowFlare.exe draws it (0x407a60) and handles its clicks (0x447790).
#include <algorithm>
#include <format>
#include <string>

#include "game/game.h"

namespace sf {

namespace {

constexpr int kPerPage = 6;
constexpr int kPages = (kSpellCount + kPerPage - 1) / kPerPage;  // 4 (0xa93c)
constexpr int kFirstRow = 59;  // a row's icon top; rows are 48 apart
constexpr int kRowHeight = 48;
constexpr int kAboveBar = 412;  // clicks above the panel while it is open belong to the window

constexpr SDL_Color kName{224, 192, 128, 255};
constexpr SDL_Color kLabel{160, 160, 64, 255};
constexpr SDL_Color kValue{224, 224, 224, 255};
constexpr SDL_Color kLow{224, 64, 64, 255};
constexpr SDL_Color kShadow{0, 0, 0, 255};

// The pictures: the original's Status.njp patterns, or the open pack's
// Magic.sheet. Icons come from MagicIcon in both (spell + 2).
struct Parts {
    int window, slot, prev, next;
};
constexpr Parts kOriginalParts{6, 32, 69, 70};
constexpr Parts kOpenParts{0, 1, 2, 3};

// The page buttons (0x447790): drawn by their patterns' own offsets.
constexpr SDL_Rect kPrevButton{16, 335, 33, 16};
constexpr SDL_Rect kNextButton{271, 335, 33, 16};
// The tabs along the top: STATUS on the left half, MAGIC on the right.
constexpr int kTabBottom = 37;

bool inside(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

int rowTop(int row) { return kFirstRow + row * kRowHeight; }
// A learned spell's icon, the one place a spell can be dragged from.
SDL_Rect iconRect(int row) { return {24, rowTop(row) - 3, 32, 32}; }
// The window's own bar of eight slots.
SDL_Rect barSlot(int slot) { return {29 + 32 * slot, 356, 32, 32}; }
// The spell's name; hovering it shows its description.
SDL_Rect nameRect(int row) { return {60, rowTop(row) + 7, 168, 12}; }

// Text at 6-pixel columns from x = 60, as the exe lays its strings out
// with spaces.
int column(int c) { return 60 + c * 6; }

// The second label on a row's lower line, by spell: Attack for the spells
// that hurt, a percentage for the rest, nothing for Transport, Moon,
// Berserker and Identify.
const char* powerLabel(int spell) {
    switch (spell) {
        case kHeal: return "Heal.";
        case kEnergyShield: return "Def.";
        case kSonicBlade: return "Attack.";
        case kMagicShield: return "Shield.";
        case kCounterBurst: return "RefPer.";
        case kTransport: case kMoon: case kBerserker: case kIdentify: return nullptr;
        default: return "Attack.";
    }
}
bool powerInPercent(int spell) {
    return spell == kHeal || spell == kEnergyShield || spell == kSonicBlade || spell == kMagicShield || spell == kCounterBurst;
}

}  // namespace

void Game::openMagicWindow(int page) {
    if (!magicOpen_) toggleMagicWindow();
    magicPage_ = std::clamp(page, 0, kPages - 1);
}

// M or the Settings Menu's row. It takes the left of the screen from the
// STATUS window, and not while a shop has it.
void Game::toggleMagicWindow() {
    if (magicOpen_) {
        closeMagicWindow();
        return;
    }
    if (openShop_ || dialogue_ || !player_) return;
    showStatus_ = false;
    closeMissions();
    closeAreaMap();
    closeWarpWindow();
    helpOpen_ = false;
    magicOpen_ = true;
}

void Game::closeMagicWindow() {
    magicOpen_ = false;
    dragSpell_ = -1;
}

bool Game::magicWindowClick(int x, int y, bool right) {
    if (!magicOpen_) return false;
    if (x >= 320 || y >= kAboveBar) return false;
    if (right) return true;
    if (y < kTabBottom && x < 160) {
        closeMagicWindow();
        showStatus_ = true;
        return true;
    }
    if (magicPage_ > 0 && inside(kPrevButton, x, y)) {
        --magicPage_;
        return true;
    }
    if (magicPage_ < kPages - 1 && inside(kNextButton, x, y)) {
        ++magicPage_;
        return true;
    }
    for (int row = 0; row < kPerPage; ++row) {
        const int spell = magicPage_ * kPerPage + row;
        if (spell >= kSpellCount) break;
        if (spells_.knows(spell) && inside(iconRect(row), x, y)) dragSpell_ = spell;
    }
    for (int slot = 0; slot < kSpellBarSize; ++slot) {
        const int spell = spells_.bar[static_cast<std::size_t>(slot)];
        if (spells_.knows(spell) && inside(barSlot(slot), x, y)) dragSpell_ = spell;
    }
    return true;
}

// Letting go of a dragged spell over a slot of the bar puts it there, and
// takes it out of any other slot it was in. Let go anywhere else, nothing
// changes.
void Game::magicWindowRelease(int x, int y) {
    if (dragSpell_ < 0) return;
    const int spell = dragSpell_;
    dragSpell_ = -1;
    if (!magicOpen_) return;
    for (int slot = 0; slot < kSpellBarSize; ++slot) {
        if (!inside(barSlot(slot), x, y)) continue;
        for (int& s : spells_.bar) {
            if (s == spell) s = -1;
        }
        spells_.bar[static_cast<std::size_t>(slot)] = spell;
        return;
    }
}

void Game::renderMagicWindow() {
    if (!magicOpen_ || openShop_ || !player_) return;
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
    if (!bank || bank->patterns.size() <= 70) {
        bank = itemBank("System/Game/Pattern/Magic");
        parts = kOpenParts;
    }
    const SpriteBank* icons = itemBank("System/Game/Pattern/MagicIcon");
    auto draw = [&](const SpriteBank* b, int pattern, int x = 0, int y = 0, int alpha = 1000) {
        const SpriteTexture* t = b ? sprites_.get(*b, pattern, false) : nullptr;
        if (!t) return false;
        SDL_SetTextureAlphaMod(t->texture, static_cast<Uint8>(alpha * 255 / 1000));
        const SDL_Rect dst{x + t->originX, y + t->originY, t->width, t->height};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        SDL_SetTextureAlphaMod(t->texture, 255);
        return true;
    };
    // In Font01, with lowercase, as the window's strings are written.
    auto text = [&](int x, int y, std::string_view s, SDL_Color c) { windowText(x, y, s, c); };

    if (!draw(bank, parts.window)) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 20, 18, 26, 225);
        const SDL_Rect r{0, 0, 320, 416};
        SDL_RenderFillRect(renderer_, &r);
        text(16, 16, "STATUS", kLabel);
        text(176, 16, "MAGIC", kName);
    }

    int hovered = -1;
    for (int row = 0; row < kPerPage; ++row) {
        const int spell = magicPage_ * kPerPage + row;
        if (spell >= kSpellCount) break;
        const int y = rowTop(row);
        draw(bank, parts.slot, 24, y - 3);
        if (!spells_.knows(spell)) continue;
        draw(icons, spell + 2, 27, y);
        if (inside(nameRect(row), mx, my)) hovered = spell;

        const auto& info = magic_.spell(spell);
        const int own = spells_.level[static_cast<std::size_t>(spell)];
        const int cast = castLevel(spell);
        text(58, y + 7, info.name, kName);

        // Line 1: level and uses towards the next one.
        text(column(22), y - 1, "Lv.", kLabel);
        text(column(29), y - 1, "Exp.", kLabel);
        const SDL_Color levelColor = cast > own ? kName : cast < own ? kLow : kValue;
        text(column(24) + 6, y - 1, std::format("{:3d}", cast), levelColor);
        if (own >= kMaxSpellLevel) {
            text(column(34), y - 1, "Max", kName);
        } else {
            text(column(33), y - 1, std::format("{:3d}", spells_.uses[static_cast<std::size_t>(spell)]), kValue);
            // "%3d/%3d" (0x47d80c): the most is padded too.
            text(column(36), y - 1, "/", kLabel);
            text(column(37), y - 1, std::format("{:3d}", info.usesToLevel[static_cast<std::size_t>(own - 1)]), kValue);
        }

        // Line 2: MP and power at the level it is cast at.
        text(column(22), y + 11, "MP.", kLabel);
        const int baseCost = magic_.mpCost(spell, cast);
        const int cost = mpCost(spell, cast);
        const SDL_Color costColor = cost > baseCost ? kLow : cost < baseCost ? kName : kValue;
        text(column(25), y + 11, std::format("{:3d}", cost), costColor);
        if (const char* label = powerLabel(spell)) {
            text(column(29), y + 11, label, kLabel);
            const int power = magic_.power(spell, cast);
            if (powerInPercent(spell)) {
                text(column(36), y + 11, std::format("{:3d}", power), kValue);
                text(column(39), y + 11, "%", kLabel);
            } else {
                text(column(36), y + 11, std::format("{:4d}", power), kValue);
            }
        }
    }

    if (magicPage_ > 0) draw(bank, parts.prev);
    if (magicPage_ < kPages - 1) draw(bank, parts.next);
    for (int slot = 0; slot < kSpellBarSize; ++slot) {
        const int spell = spells_.bar[static_cast<std::size_t>(slot)];
        if (spells_.knows(spell)) draw(icons, spell + 2, 32 + 32 * slot, 359);
    }

    // The description under the mouse, centred on it and kept on screen,
    // with a faint white edge.
    if (hovered >= 0 && dragSpell_ < 0) {
        const auto& lines = magic_.spell(hovered).description;
        int w = 0;
        for (const auto& line : lines) w = std::max(w, uiTextWidth(line));
        const int h = static_cast<int>(lines.size()) * 12;
        if (w > 0) {
            int x = std::max(1, mx - w / 2), y = std::max(1, my + 8);
            x = std::min(x, 640 - w);
            y = std::min(y, 480 - h);
            for (std::size_t i = 0; i < lines.size(); ++i) text(x, y + static_cast<int>(i) * 12, lines[i], kValue);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 128);
            const SDL_Rect edge{x - 1, y - 1, w + 1, h + 1};
            SDL_RenderDrawRect(renderer_, &edge);
        }
    }

    if (dragSpell_ >= 0) draw(icons, dragSpell_ + 2, mx - 3, my - 3);
}

}  // namespace sf
