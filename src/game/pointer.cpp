// The mouse pointer, the Click Range square round it and the name over
// what it points at, as ShadowFlare.exe draws them (0x4087b0, 0x416bb0 and
// 0x40ee70). See docs/controls.md and docs/formats.md "The pointer".

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "game/game.h"
#include "platform/filesystem.h"

namespace sf {

namespace {

// The square's half size for each Click Range size, MINI to MAX (0x416bb0
// and the pick at 0x4165d0 share the table; MINI draws none).
constexpr int kSquareHalf[Settings::kClickSizes] = {0, 12, 16, 24, 48};
// The panel along the bottom: no square or name over it (y 0x198).
constexpr int kPanelTop = 408;

// How opaque the square is, per mille: faint, brighter with something
// under it (0x416bb0 passes 100 or 300).
constexpr int kSquareFaint = 100, kSquareBright = 300;

// The name's colours (0x40ee70): a monster's box is its health, dark red
// for what it has left and black for what it has lost; people, objects
// and items have a navy box. All at half opacity, edged in black at 800.
constexpr SDL_Color kHealthLeft{0x80, 0x20, 0x20, 128};
constexpr SDL_Color kHealthLost{0, 0, 0, 128};
constexpr SDL_Color kNavy{0, 0, 0x60, 128};
// Ours: an item's name box while the pointer is on it, with every item named.
constexpr SDL_Color kNavyLit{0x20, 0x30, 0xa0, 200};
// The edge: the exe asks for black at 800 (flag 0x400), but the manual's
// screenshots show a soft dark rim with a lighter line inside along the
// top and left, so it is drawn that way.
constexpr SDL_Color kEdge{0, 0, 0, 72};
constexpr SDL_Color kRimLight{255, 255, 255, 28};
constexpr SDL_Color kRimDark{0, 0, 0, 40};
constexpr SDL_Color kShadow{0, 0, 0, 255};
// Element orbs when the data has no StatusIcon bank: fire, water, earth,
// thunder, holy, dark, gel, metal.
constexpr SDL_Color kElementColors[] = {{200, 40, 30, 255},  {50, 110, 210, 255}, {150, 100, 60, 255},  {220, 200, 40, 255},
                                        {60, 120, 220, 255}, {150, 70, 180, 255}, {170, 170, 170, 255}, {70, 70, 70, 255}};
constexpr int kCharWidth = 6;    // Font01's cells
constexpr int kLabelLift = 24;   // the name's top above where it floats from

SDL_Color rgb(std::uint32_t color) {
    return {static_cast<Uint8>(color >> 16), static_cast<Uint8>(color >> 8), static_cast<Uint8>(color), 255};
}

void fill(SDL_Renderer* r, int left, int top, int right, int bottom, SDL_Color c) {
    if (right < left || bottom < top) return;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    const SDL_Rect rect{left, top, right - left + 1, bottom - top + 1};
    SDL_RenderFillRect(r, &rect);
}

void edge(SDL_Renderer* r, int left, int top, int right, int bottom, SDL_Color c) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    const SDL_Rect rect{left, top, right - left + 1, bottom - top + 1};
    SDL_RenderDrawRect(r, &rect);
}

// The name box's edge and rims round a box from left to right whose text
// starts at y (0x40ee70).
void nameFrame(SDL_Renderer* r, int left, int right, int y) {
    edge(r, left - 1, y - 3, right + 1, y + 14, kEdge);
    fill(r, left, y - 2, right, y - 2, kRimLight);
    fill(r, left, y - 1, left, y + 13, kRimLight);
    fill(r, left + 1, y + 13, right, y + 13, kRimDark);
    fill(r, right, y - 1, right, y + 12, kRimDark);
}

// Names are trimmed, as the original's padded ones ("  Warehouse  ")
// would otherwise sit off centre in our font.
void trim(std::string& name) {
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
}

}  // namespace

std::optional<SDL_FPoint> Game::pointerPosition() const {
    if (forcedPointer_) return forcedPointer_;
    // Only while the mouse is over the window.
    SDL_Window* window = SDL_RenderGetWindow(renderer_);
    if (!window || SDL_GetMouseFocus() != window) return std::nullopt;
    int mx, my;
    SDL_GetMouseState(&mx, &my);
    SDL_FPoint p;
    SDL_RenderWindowToLogical(renderer_, mx, my, &p.x, &p.y);
    return p;
}

void Game::setPointer(std::optional<SDL_FPoint> at, int onKind) {
    forcedPointer_ = at;
    pointerOnKind_ = onKind;
}

// --pointer enemy|person|item: on the one of that kind nearest the hero
// that is on screen, where a click aims.
void Game::followPointerTarget() {
    if (pointerOnKind_ < 0 || !player_) return;
    double best = INFINITY;
    std::optional<SDL_FPoint> at;
    auto consider = [&](double x, double y, int lift) {
        int sx, sy;
        worldToScreen(static_cast<int>(x), static_cast<int>(y), sx, sy);
        const auto [px, py] = onScreen(sx, sy - lift);
        if (px < 0 || py < 0 || px >= kScreenWidth || py >= kPanelTop) return;
        const double d = std::hypot(x - player_->x, y - player_->y);
        if (d < best) {
            best = d;
            at = SDL_FPoint{px, py};
        }
    };
    if (pointerOnKind_ == Settings::kItem) {
        for (const auto& g : world_.groundItems) consider(g.x, g.y, 8);
    } else if (pointerOnKind_ == Settings::kObject) {
        // The middle of each usable object, where a click would go.
        for (std::size_t i = 0; i < world_.control.placements.size(); ++i) {
            const auto& p = world_.control.placements[i];
            if (p.group != ScenarioControl::Group::Object) continue;
            const double x = p.x + (p.rect[0] + p.rect[2]) / 2.0, y = p.y + (p.rect[1] + p.rect[3]) / 2.0;
            int sx, sy;
            worldToScreen(static_cast<int>(x), static_cast<int>(y), sx, sy);
            const auto [px, py] = onScreen(sx, sy);
            if (usableObjectAt(px, py) == i) consider(x, y, 0);
        }
    } else {
        for (const auto& a : world_.actors) {
            const bool want = pointerOnKind_ == Settings::kEnemy ? a.hostile() && a.alive() : !a.hostile();
            if (want && a.active) consider(a.character.x, a.character.y, 40);
        }
    }
    if (at) forcedPointer_ = at;
}

// Whether the world under the pointer is uncovered: not the panel, not a
// window (the item window takes the right half, status, magic, GATE, the
// map and shops the left; 0x416fb0), and nothing else holding the screen.
bool Game::pointerOnWorld(const SDL_FPoint& p) const {
    if (!player_ || menuOpen_ || optionsOpen_ || newCharacter_ || dialogue_ || visual_ || missionsOpen_ || p.y >= kPanelTop) return false;
    const bool left = showStatus_ || magicOpen_ || warpOpen_ || mapOpen_ || openShop_.has_value();
    const bool right = rightWindowOpen();
    if (left && right) return false;
    if (right && p.x >= kScreenWidth / 2) return false;
    if (left && p.x < kScreenWidth / 2) return false;
    return true;
}

// What a click would go for under the pointer, worked out once a frame
// before the world is drawn so the one it lights is drawn brighter.
void Game::updateHover() {
    hover_ = {};
    hoverOn_ = false;
    followPointerTarget();
    const auto p = pointerPosition();
    if (!p || !pointerOnWorld(*p)) return;
    hoverOn_ = true;
    hover_ = clickTarget(p->x, p->y, ~0u);
}

const Actor* Game::hoveredActor() const {
    if ((hover_.kind == Settings::kEnemy || hover_.kind == Settings::kPerson) && hover_.index < world_.actors.size()) return &world_.actors[hover_.index];
    return nullptr;
}

// The Click Range square (0x416bb0): red on a monster, yellow on an item,
// white otherwise, faint unless something is under it.
void Game::renderClickSquare() {
    if (!hoverOn_ || !settings_.clickRange) return;
    const int half = kSquareHalf[std::clamp(settings_.clickSize, 0, Settings::kClickSizes - 1)];
    const auto p = pointerPosition();
    if (half <= 0 || !p) return;
    SDL_Color c{255, 255, 255, 0};
    if (hover_.kind == Settings::kEnemy) c = {255, 0, 0, 0};
    if (hover_.kind == Settings::kItem) c = {0xe0, 0xe0, 0, 0};
    c.a = static_cast<Uint8>((hover_.kind >= 0 ? kSquareBright : kSquareFaint) * 255 / 1000);
    const int x = static_cast<int>(p->x), y = static_cast<int>(p->y);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    const SDL_Rect r{x - half, y - half, 2 * half + 1, 2 * half + 1};
    SDL_RenderDrawRect(renderer_, &r);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
}

// The name of what the pointer is on (0x40ee70), centred 24 pixels over
// the height its placement gives. A monster's box shows its health and
// its element's orb sits in a wide space before the name.
void Game::renderHoverName() {
    if (!hoverOn_ || hover_.kind < 0) return;
    std::string name;
    SDL_Color color = rgb(0xe0e0e0);
    double wx = 0, wy = 0;
    int height = 0;
    const Actor* monster = nullptr;
    const bool openPack = data_->exists("content.txt");
    switch (hover_.kind) {
        case Settings::kEnemy:
        case Settings::kPerson: {
            const Actor& a = world_.actors[hover_.index];
            // The open pack's people are named for its scripts, not shown.
            if (!a.hostile() && openPack) return;
            name = toFontText(a.placement.name);
            color = rgb(a.placement.nameColor);
            height = a.placement.nameHeight;
            wx = a.character.x;
            wy = a.character.y;
            if (a.hostile()) monster = &a;
            break;
        }
        case Settings::kObject: {
            if (openPack) return;
            const auto& p = world_.control.placements[hover_.index];
            name = toFontText(p.name);
            color = rgb(p.nameColor);
            height = p.nameHeight;
            wx = p.x;
            wy = p.y;
            break;
        }
        case Settings::kItem: {
            // With every item named, its own name is already there.
            if (itemNamesShown()) return;
            const GroundItem& g = world_.groundItems[hover_.index];
            const ItemDef& def = *g.item.def;
            name = def.isGold() ? std::format("{} Gold", g.item.count) : toFontText(g.item.shownName());
            color = qualityColor(def.quality);
            wx = g.x;
            wy = g.y;
            break;
        }
        default: return;  // the companion has none
    }
    trim(name);
    if (name.empty()) return;

    int sx, sy;
    worldToScreen(static_cast<int>(wx), static_cast<int>(wy), sx, sy);
    const SDL_FPoint at = onScreen(sx, sy - height);
    const int x = static_cast<int>(at.x) + 2;
    const int y = static_cast<int>(at.y) - kLabelLift;
    const int element = monster ? monster->placement.stats.element : -1;
    const bool orb = element >= 0 && element < 8;
    // The orb's space is a full-width blank, two characters wide.
    const int chars = static_cast<int>(name.size()) + (orb ? 2 : 0);
    const int half = chars * kCharWidth / 2;
    const int left = x - half - 4, right = x + half + 1;
    if (monster) {
        const int width = right - left + 1;
        const int hp = std::clamp(monster->hp, 0, monster->maxHp);
        const int filled = hp >= monster->maxHp ? width : width * hp / std::max(1, monster->maxHp);
        fill(renderer_, left, y - 2, left + filled - 1, y + 13, kHealthLeft);
        fill(renderer_, left + filled, y - 2, right, y + 13, kHealthLost);
    } else {
        fill(renderer_, left, y - 2, right, y + 13, kNavy);
    }
    nameFrame(renderer_, left, right, y);
    const int textX = x - half + (orb ? 2 * kCharWidth : 0);
    windowText(textX, y, name, color);
    if (orb) {
        // StatusIcon.njp patterns 3-10, the open pack's Elements sheet, or
        // a plain dot.
        const SpriteBank* icons = itemBank("System/Game/Pattern/StatusIcon");
        const SpriteTexture* icon = icons && icons->patterns.size() > 10 ? sprites_.get(*icons, element + 3, false) : nullptr;
        const SpriteBank* ours = icon ? nullptr : itemBank("System/Game/Pattern/Elements");
        if (ours && static_cast<int>(ours->patterns.size()) > element) icon = sprites_.get(*ours, element, false);
        // Centred in the box's first 16 pixels, before the name.
        const int w = icon ? icon->width : 12, h = icon ? icon->height : 11;
        const int ix = left + (16 - w) / 2, iy = y - 2 + (16 - h + 1) / 2;
        if (icon) {
            const SDL_Rect dst{ix, iy, icon->width, icon->height};
            SDL_RenderCopy(renderer_, icon->texture, nullptr, &dst);
        } else {
            const SDL_Color c = kElementColors[element];
            fill(renderer_, ix + 3, iy + 1, ix + 8, iy + 9, kShadow);
            fill(renderer_, ix + 4, iy + 2, ix + 7, iy + 8, c);
            fill(renderer_, ix + 2, iy + 3, ix + 9, iy + 7, kShadow);
            fill(renderer_, ix + 3, iy + 3, ix + 8, iy + 7, c);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
}

// Ours (Rules::groundItemNames): Alt held, or L to keep them on.
bool Game::itemNamesShown() const {
    if (!rules_.groundItemNames || !world_.map || world_.groundItems.empty()) return false;
    return forceItemNames_ || settings_.itemNames || (SDL_GetModState() & KMOD_ALT) != 0;
}

// Every item on screen gets a name box as the pointer's (0x40ee70), where
// that one would sit, stacked upward where they would cover each other so
// a pile of loot reads as a list. The nearest the front keep their place.
// Boxes that would reach the panel or leave the screen are left out.
std::vector<Game::ItemLabel> Game::itemLabels() const {
    std::vector<ItemLabel> labels;
    if (!itemNamesShown()) return labels;
    struct Anchor {
        std::size_t item;
        float x, y;
    };
    std::vector<Anchor> anchors;
    for (std::size_t i = 0; i < world_.groundItems.size(); ++i) {
        int sx, sy;
        worldToScreen(static_cast<int>(world_.groundItems[i].x), static_cast<int>(world_.groundItems[i].y), sx, sy);
        const SDL_FPoint at = onScreen(sx, sy);
        if (at.x < -64 || at.x > kScreenWidth + 64 || at.y < 0 || at.y > kPanelTop + kLabelLift) continue;
        anchors.push_back({i, at.x, at.y});
    }
    std::ranges::stable_sort(anchors, [](const Anchor& a, const Anchor& b) { return a.y > b.y; });
    std::vector<LabelBox> boxes;
    for (const Anchor& a : anchors) {
        const GroundItem& g = world_.groundItems[a.item];
        const ItemDef& def = *g.item.def;
        std::string name = def.isGold() ? std::format("{} Gold", g.item.count) : toFontText(g.item.shownName());
        trim(name);
        if (name.empty()) continue;
        // As renderHoverName lays out an item's name.
        const int x = static_cast<int>(a.x) + 2, y = static_cast<int>(a.y) - kLabelLift;
        const int half = static_cast<int>(name.size()) * kCharWidth / 2;
        const int left = x - half - 4, right = x + half + 1;
        labels.push_back({a.item, {left - 1, y - 3, right - left + 3, 18}, std::move(name), qualityColor(def.quality)});
        boxes.push_back(labels.back().box);
    }
    stackLabels(boxes);
    std::vector<ItemLabel> shown;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const LabelBox& b = boxes[i];
        if (b.y < 0 || b.y + b.h > kPanelTop || b.x + b.w <= 0 || b.x >= kScreenWidth) continue;
        labels[i].box = b;
        shown.push_back(std::move(labels[i]));
    }
    return shown;
}

void Game::renderItemNames() {
    const auto labels = itemLabels();
    // Front to back as laid out; drawn back to front so nothing is under
    // a box placed before it.
    for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
        const LabelBox& b = it->box;
        const int left = b.x + 1, right = b.x + b.w - 2, y = b.y + 3;
        const bool lit = hovered(Settings::kItem, it->item);
        fill(renderer_, left, y - 2, right, y + 13, lit ? kNavyLit : kNavy);
        nameFrame(renderer_, left, right, y);
        windowText(left + 4, y, it->name, it->color);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
}

// The pointer itself, over everything (0x4087b0): System.njp pattern 0,
// or 1 (with a question mark) while Identify waits for an item. The
// system's own pointer is hidden while ours can be drawn.
void Game::renderPointer() {
    const SpriteBank* bank = itemBank("System/Common/Pattern/System");
    const int pattern = identifying_ ? 1 : 0;
    const SpriteTexture* sprite = bank && static_cast<int>(bank->patterns.size()) > pattern ? sprites_.get(*bank, pattern, false) : nullptr;
    const bool ours = sprite != nullptr;
    if (ours != pointerHidden_) {
        SDL_ShowCursor(ours ? SDL_DISABLE : SDL_ENABLE);
        pointerHidden_ = ours;
    }
    const auto p = pointerPosition();
    if (!sprite || !p) return;
    const SDL_Rect dst{static_cast<int>(p->x) + sprite->originX, static_cast<int>(p->y) + sprite->originY, sprite->width, sprite->height};
    SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
}

// enemies.txt names the open pack's monsters by kind.
void Game::nameOpenEnemy(Actor& actor) {
    if (!actor.hostile() || !actor.placement.name.empty()) return;
    if (!enemyNames_) {
        enemyNames_.emplace();
        if (auto text = data_->read("enemies.txt")) {
            try {
                *enemyNames_ = open::parseEnemyNames(*text);
            } catch (const FormatError& e) {
                log::warn("enemies.txt: {}", e.what());
            }
        }
    }
    const auto it = enemyNames_->find(actor.placement.kind);
    if (it == enemyNames_->end()) return;
    actor.placement.name = it->second.name;
    actor.placement.nameHeight = it->second.height;
    actor.placement.nameColor = it->second.color;
}

}  // namespace sf
