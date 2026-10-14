// The GATE window: the warp gates the hero has found, ten to a page in the
// left half of the screen; a click on one goes to it. Clicking a gate runs
// its WARPGATE, which opens the window (ShadowFlare.exe 0x4334da); walking
// away runs DELETEWARPGATE, which closes it if that gate opened it
// (0x433544). Laid out as the exe draws it and takes its clicks, both in
// 0x40c950.
#include <algorithm>
#include <format>
#include <string>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "game/game.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

constexpr int kPerPage = 10;
// Row r's button is at (52, 59 + 30 r), its name at (79, 65 + 30 r); the
// mouse is over the row from x 32 to 288 and y 63 + 30 r to 85 + 30 r.
constexpr int kButtonX = 52, kButtonY = 59, kRowStep = 30;
constexpr int kNameX = 79, kNameY = 65;
constexpr int kHoverLeft = 32, kHoverRight = 288, kHoverTop = 63, kHoverBottom = 85;
// The page arrows, and the page number centred between them on y 370.
constexpr SDL_Rect kPrevButton{28, 368, 65, 19};
constexpr SDL_Rect kNextButton{224, 368, 65, 19};
constexpr int kPageLeft = 85, kPageWidth = 153, kPageY = 370;
// Clicks left of x 320 and above y 412 belong to the window (0x448490);
// a right click there closes it (0x44acd0).
constexpr int kClickRight = 320, kClickBottom = 412;

constexpr SDL_Color kLit{224, 224, 224, 255};
constexpr SDL_Color kDim{128, 128, 128, 255};
constexpr SDL_Color kShadow{0, 0, 0, 255};

bool inside(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

bool overRow(int row, int x, int y) {
    const int top = kRowStep * row;
    return x >= kHoverLeft && x <= kHoverRight && y >= kHoverTop + top && y <= kHoverBottom + top;
}

int pageCount(int gates) { return (gates + kPerPage - 1) / kPerPage; }

// The pictures: the original's Status.njp, or our pack's Warp sheet, which
// lists them in this order.
struct Parts {
    int window, prev, next, button, pressed, lit;
};
constexpr Parts kOriginalParts{13, 11, 12, 22, 23, 24};
constexpr Parts kOpenParts{0, 1, 2, 3, 4, 5};

}  // namespace

// Table.Tbd table 40, or the pack's gates.txt.
void Game::loadWarpGates() {
    if (!tables_.tables.empty()) {
        warpGates_ = WarpGates::fromTables(tables_);
    } else if (auto text = data_->read("gates.txt")) {
        try {
            warpGates_ = open::parseGates(*text);
        } catch (const FormatError& e) {
            log::warn("gates.txt: {}", e.what());
        }
    }
    for (auto& g : warpGates_.list) g.name = toAscii(g.name);
    log::info("Loaded {} warp gates", warpGates_.count());
}

// Every gate whose WARP flag is set, in table order. Numbers gates.txt
// skips have no name and are left out.
std::vector<int> Game::warpGatesFound() const {
    std::vector<int> found;
    for (int g = 0; g < warpGates_.count(); ++g) {
        if (warpGates_.list[static_cast<std::size_t>(g)].name.empty()) continue;
        const auto it = flags_.find({ScenarioScript::kWarp, g});
        if (it != flags_.end() && it->second != 0) found.push_back(g);
    }
    return found;
}

// WARPGATE: not while the window is open already or a shop has the
// screen. Opening it closes every other window (0x4170f0).
void Game::openWarpGate(int gate, int page) {
    if (warpOpen_ || openShop_ || !player_) return;
    if (rightWindowOpen()) closeInventory();
    showStatus_ = false;
    closeMissions();
    closeAreaMap();
    closeMagicWindow();
    helpOpen_ = false;
    warpOpen_ = true;
    warpOpener_ = gate;
    warpPage_ = std::clamp(page, 0, std::max(0, pageCount(static_cast<int>(warpGatesFound().size())) - 1));
    log::info("Gate {} opens the GATE window: {} gates found", gate, warpGatesFound().size());
}

void Game::closeWarpGate(int gate) {
    if (warpOpen_ && warpOpener_ == gate) closeWarpWindow();
}

// The n-th gate on the page showing: the party goes to its scenario and
// entry, as CHANGESCENARIO would take them, and every window closes.
void Game::chooseWarpGate(int row) {
    if (!warpOpen_ || row < 0 || row >= kPerPage) return;
    const auto found = warpGatesFound();
    const std::size_t index = static_cast<std::size_t>(warpPage_ * kPerPage + row);
    if (index >= found.size()) return;
    const auto* gate = warpGates_.find(found[index]);
    if (!gate) return;
    log::info("Warp gate {} \"{}\": to scenario {}, entry {}", found[index], gate->name, gate->scenario, gate->entry);
    playSound(0x3a);  // (0x40c950)
    closeWarpWindow();
    if (rightWindowOpen()) closeInventory();
    showStatus_ = false;
    closeMissions();
    closeAreaMap();
    closeMagicWindow();
    walkTarget_.reset();
    attackTarget_.reset();
    pendingScenario_.emplace(gate->scenario, gate->entry);
}

bool Game::warpGateClick(int x, int y, bool right) {
    if (!warpOpen_ || x >= kClickRight || y >= kClickBottom) return false;
    if (right) {
        closeWarpWindow();
        return true;
    }
    const int found = static_cast<int>(warpGatesFound().size());
    const int pages = pageCount(found);
    for (int row = 0; row < kPerPage && warpPage_ * kPerPage + row < found; ++row) {
        if (overRow(row, x, y)) {
            chooseWarpGate(row);
            return true;
        }
    }
    if (warpPage_ > 0 && inside(kPrevButton, x, y)) {
        --warpPage_;
        playSound(0x3a);
    } else if (warpPage_ < pages - 1 && pages > 1 && inside(kNextButton, x, y)) {
        ++warpPage_;
        playSound(0x3a);
    }
    return true;
}

void Game::renderWarpGate() {
    if (!warpOpen_) return;
    int mx = 0, my = 0;
    bool held = false;
    {
        int wx, wy;
        held = (SDL_GetMouseState(&wx, &wy) & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
        mx = static_cast<int>(lx);
        my = static_cast<int>(ly);
    }
    // The original's pictures, our pack's, or plain boxes without either.
    const SpriteBank* bank = itemBank("System/Game/Pattern/Status");
    Parts parts = kOriginalParts;
    if (!bank || bank->patterns.size() <= 24) {
        bank = itemBank("System/Game/Pattern/Warp");
        parts = kOpenParts;
        if (bank && bank->patterns.size() <= 5) bank = nullptr;
    }
    auto draw = [&](int pattern, int x = 0, int y = 0) {
        const SpriteTexture* t = bank ? sprites_.get(*bank, pattern, false) : nullptr;
        if (!t) return false;
        const SDL_Rect dst{x + t->originX, y + t->originY, t->width, t->height};
        SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        return true;
    };
    auto box = [&](const SDL_Rect& r, SDL_Color fill, SDL_Color edge) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, fill.r, fill.g, fill.b, fill.a);
        SDL_RenderFillRect(renderer_, &r);
        SDL_SetRenderDrawColor(renderer_, edge.r, edge.g, edge.b, edge.a);
        SDL_RenderDrawRect(renderer_, &r);
    };

    if (!draw(parts.window)) {
        box({0, 0, 320, 416}, {44, 6, 6, 235}, {150, 130, 90, 255});
        uiText(160 - uiTextWidth("GATE") / 2, 16, "GATE", kLit);
    }

    const auto found = warpGatesFound();
    const int pages = pageCount(static_cast<int>(found.size()));
    for (int row = 0; row < kPerPage; ++row) {
        const std::size_t index = static_cast<std::size_t>(warpPage_ * kPerPage + row);
        if (index >= found.size()) break;
        const int top = kRowStep * row;
        const bool over = overRow(row, mx, my);
        // Lit under the mouse, pushed in while the button is held, and the
        // name pushed a pixel with it.
        const int pattern = !over ? parts.button : held ? parts.pressed : parts.lit;
        const int push = over && held ? 1 : 0;
        if (!draw(pattern, kButtonX, kButtonY + top)) {
            box({kButtonX + 2, kButtonY + top + 2, 20, 20}, over ? SDL_Color{160, 124, 66, 255} : SDL_Color{90, 70, 40, 255},
                {200, 170, 110, 255});
        }
        if (const auto* gate = warpGates_.find(found[index]); gate && !gate->name.empty()) {
            uiText(kNameX + 1 + push, kNameY + 1 + top + push, gate->name, kShadow);
            uiText(kNameX + push, kNameY + top + push, gate->name, over ? kLit : kDim);
        }
    }
    if (warpPage_ > 0 && !draw(parts.prev)) box({45, 368, 33, 16}, {90, 70, 40, 255}, {200, 170, 110, 255});
    if (warpPage_ < pages - 1 && pages > 1 && !draw(parts.next)) box({243, 368, 32, 16}, {90, 70, 40, 255}, {200, 170, 110, 255});
    if (pages > 0) {
        // "%2d / %2d", centred in the strip between the arrows.
        const std::string page = std::format("{:2d} / {:2d}", warpPage_ + 1, pages);
        const int x = ((kPageWidth - uiTextWidth(page)) >> 1) + kPageLeft;
        uiText(x, kPageY + 1, page, kShadow);
        uiText(x - 1, kPageY, page, kLit);
    }
}

}  // namespace sf
