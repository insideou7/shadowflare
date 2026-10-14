// The special items window (X): where quest items, gems, stones, cards and
// the element medicines are kept, each on its own place in one of four
// sections, in the item window's place on the right of the screen. The
// rules are in logic/inventory.cpp (Inventory::addSpecial); this file opens,
// draws and clicks the window, after ShadowFlare.exe's 0x407170 (items),
// 0x40f900 (picture), 0x408db0 (hover), 0x448620 (left button) and
// 0x44ae20 (right button).
#include <format>

#include "game/game.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr int kCell = 32;
// The window takes the right half above the panel; clicks there are its
// own (0x448620 answers for x >= 320, y < 412).
constexpr SDL_Rect kWindow{320, 0, 320, 412};
// Where each section's cells start on the screen (the table at 0x407170):
// 0 quest items, 1 gems, 2 stones and cards, 3 medicines.
constexpr int kSectionX = 336;
constexpr int kSectionY[Inventory::kSpecialPages] = {72, 168, 232, 296};
constexpr const char* kSectionNames[Inventory::kSpecialPages] = {"QUEST ITEMS", "GEMS", "STONES AND CARDS", "MEDICINES"};
// The button at the bottom of both the item and special windows that
// switches between them (0x445d10 area 0x177 < x < 0x1bb, 0x189 < y <
// 0x193). The original lights it, patterns 0x73 and 0x75, under the
// pointer.
constexpr SDL_Rect kSwitchButton{376, 394, 67, 9};

bool inside(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

SDL_Rect sectionRect(int page) {
    return {kSectionX, kSectionY[page], Inventory::kSpecialWidth * kCell, Inventory::kSpecialHeights[page] * kCell};
}

// The section and cell a screen point is over, if any.
bool specialCellAt(int x, int y, int& page, int& cx, int& cy) {
    for (int p = 0; p < Inventory::kSpecialPages; ++p) {
        const SDL_Rect r = sectionRect(p);
        if (!inside(r, x, y)) continue;
        page = p;
        cx = (x - r.x) / kCell;
        cy = (y - r.y) / kCell;
        return true;
    }
    return false;
}

void frame(SDL_Renderer* renderer, const SDL_Rect& r, SDL_Color fill, SDL_Color edge) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, edge.r, edge.g, edge.b, edge.a);
    SDL_RenderDrawRect(renderer, &r);
}

}  // namespace

// X opens the window, closing the item window (and with it a shop's
// trade), or closes it; not while trading (0x442b85).
void Game::toggleSpecialWindow() {
    if (!player_ || openShop_) return;
    if (specialOpen_) {
        closeInventory();
        return;
    }
    openSpecialWindow();
}

void Game::openSpecialWindow() {
    closeMissions();
    closeWarpWindow();
    showInventory_ = false;
    identifying_ = false;  // the item window closing ends Identify (0x41e4f0)
    specialOpen_ = true;
}

bool Game::specialButtonLit(int x, int y) const { return !openShop_ && inside(kSwitchButton, x, y); }

bool Game::specialWindowClick(int x, int y, bool right) {
    if (!specialOpen_) return false;
    const bool onWindow = inside(kWindow, x, y);
    if (!right) {
        // Only the button does anything; the rest of the window keeps the
        // click from the world (0x448620).
        if (onWindow && specialButtonLit(x, y)) {
            playSound(0x3a);
            openItemWindow();
        }
        return onWindow;
    }
    // The right button uses a medicine (0x44a240) and takes it away when it
    // did something. Anywhere off the sections it closes the window, and a
    // click off the window goes on to the world (0x44ae20).
    int page, cx, cy;
    if (specialCellAt(x, y, page, cx, cy)) {
        if (page == 3) {
            const int index = inventory_.specialIndexAt(page, cx, cy);
            if (index >= 0 && applyConsumable(*inventory_.specialPage(page)[static_cast<std::size_t>(index)].item.def)) {
                inventory_.removeSpecial(page, index);
            }
        }
        return true;
    }
    specialOpen_ = false;
    return onWindow;
}

// The window's picture: the open pack's Special.sheet (frame 0) or the
// original's Status.njp pattern 0x65, at their own screen positions, then
// the switch button, lit under the pointer (0x40f900). False when there is
// no picture.
bool Game::drawSpecialPicture() {
    int mx = 0, my = 0;
    if (const auto at = pointerPosition()) {
        mx = static_cast<int>(at->x);
        my = static_cast<int>(at->y);
    }
    const bool lit = specialButtonLit(mx, my);
    const SpriteBank* bank = itemBank("System/Game/Pattern/Special");
    std::vector<int> patterns = {0};
    if (lit) patterns.push_back(1);
    if (!bank) {
        bank = itemBank("System/Game/Pattern/Status");
        patterns = {0x65, lit ? 0x73 : 0x72};
    }
    bool drawn = false;
    for (const int pattern : patterns) {
        if (const SpriteTexture* sprite = bank ? sprites_.get(*bank, pattern, false) : nullptr) {
            const SDL_Rect dst{sprite->originX, sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
            drawn = true;
        }
    }
    return drawn;
}

void Game::renderSpecialWindow(int mx, int my) {
    const bool picture = drawSpecialPicture();
    if (!picture) {
        // Plain boxes stand in without a picture.
        frame(renderer_, {kWindow.x, kWindow.y, kWindow.w, 416}, {20, 18, 26, 225}, {150, 130, 90, 255});
        drawText(renderer_, 342, 24, "SPECIAL ITEMS", {235, 235, 235, 255});
        for (int p = 0; p < Inventory::kSpecialPages; ++p) {
            const SDL_Rect r = sectionRect(p);
            drawText(renderer_, r.x, r.y - 11, kSectionNames[p], {200, 190, 160, 255}, 1);
            for (int y = 0; y < Inventory::kSpecialHeights[p]; ++y) {
                for (int x = 0; x < Inventory::kSpecialWidth; ++x) {
                    frame(renderer_, {r.x + x * kCell, r.y + y * kCell, kCell, kCell}, {35, 32, 40, 255}, {80, 74, 64, 255});
                }
            }
        }
        const bool lit = specialButtonLit(mx, my);
        frame(renderer_, kSwitchButton, lit ? SDL_Color{90, 80, 50, 255} : SDL_Color{45, 40, 30, 255}, {150, 130, 90, 255});
        drawText(renderer_, kSwitchButton.x + 18, kSwitchButton.y + 1, "ITEMS", lit ? SDL_Color{255, 220, 120, 255} : SDL_Color{200, 190, 160, 255}, 1);
    }
    // Each item on its own place; the one under the pointer says what it
    // is while the hand is empty (0x408db0).
    const Item* hover = nullptr;
    for (int p = 0; p < Inventory::kSpecialPages; ++p) {
        const SDL_Rect section = sectionRect(p);
        for (const auto& placed : inventory_.specialPage(p)) {
            const SDL_Rect r{section.x + placed.x * kCell, section.y + placed.y * kCell, placed.item.def->width * kCell,
                             placed.item.def->height * kCell};
            drawItemPicture(*placed.item.def, r);
            if (inside(r, mx, my)) hover = &placed.item;
        }
    }
    if (hover && !hand_) renderItemPopup(*hover, mx, my);
}

}  // namespace sf
