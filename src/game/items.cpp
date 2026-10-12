// Items on the ground, loot, the inventory panel and equipment. The rules
// (bag, slots, layers) are in logic/inventory.cpp; this file connects them
// to the world, the mouse and the screen.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

#include "core/log.h"
#include "formats/binary_reader.h"
#include "formats/open_content.h"
#include "formats/table_data.h"
#include "game/content.h"
#include "game/game.h"
#include "platform/filesystem.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr double kPickupRange = 150.0;  // world units from the player's feet
constexpr int kItemDropChance = 15;     // percent; stands in for Table.Tbd table 30
constexpr int kMessageTicks = 3 * kTicksPerSecond;

// The item window, where ShadowFlare.exe draws it (Status.njp patterns 2
// and 3 at x = 320): the bag at (336, 264) and the slots from the table
// built at 0x407170, in screen pixels and cells.
constexpr SDL_Rect kPanel{320, 0, 320, 416};
constexpr int kCell = 32;
constexpr int kBagX = 336, kBagY = 264;

struct SlotBox {
    EquipSlot slot;
    int x, y, w, h;
    SDL_Rect rect() const { return {x, y, w * kCell, h * kCell}; }
};
constexpr SlotBox kSlots[] = {
    {EquipSlot::Helmet, 560, 16, 2, 2},      {EquipSlot::Body, 560, 88, 2, 3},
    {EquipSlot::Boots, 560, 192, 2, 2},      {EquipSlot::Weapon, 480, 16, 2, 4},
    {EquipSlot::Shield, 480, 160, 2, 3},     {EquipSlot::Accessory1, 400, 143, 1, 1},
    {EquipSlot::Accessory2, 440, 143, 1, 1}, {EquipSlot::Accessory3, 400, 183, 1, 1},
    {EquipSlot::Accessory4, 440, 183, 1, 1},
};

bool inside(const SDL_Rect& r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }

const SDL_Color kWhite{235, 235, 235, 255};
const SDL_Color kGoldColor{255, 210, 80, 255};
const SDL_Color kRed{255, 110, 90, 255};

void frame(SDL_Renderer* renderer, const SDL_Rect& r, SDL_Color fill, SDL_Color edge) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &r);
    SDL_SetRenderDrawColor(renderer, edge.r, edge.g, edge.b, edge.a);
    SDL_RenderDrawRect(renderer, &r);
}

}  // namespace

void Game::loadItems() {
    // The original keeps items in Item.Ibn, readable only with the byte
    // table inside the player's ShadowFlare.exe.
    if (auto ibn = data_->read("System/Game/Parameter/Item.Ibn")) {
        auto exe = data_->read("ShadowFlare.exe");
        if (!exe) {
            log::warn("Item.Ibn needs ShadowFlare.exe in the game folder to be read; playing without items");
        } else {
            try {
                items_ = ItemTable::parse(*ibn, itemSubstitutionTable(*exe));
            } catch (const FormatError& e) {
                log::warn("Item.Ibn: {}; playing without items", e.what());
            }
        }
    } else if (auto text = data_->read("items.txt")) {
        try {
            items_ = open::parseItems(*text);
        } catch (const FormatError& e) {
            log::warn("items.txt: {}", e.what());
        }
    }
    log::info("Loaded {} item definitions", items_.items.size());
}

// A new character's equipment (ShadowFlare.exe 0x44104c): Leather Cloth
// worn, four Tablets and four Capsules in the bag. The belt is not there yet.
void Game::giveStartingKit() {
    give(static_cast<int>(ItemDef::Class::Armour), 0, 1, GiveTo::Wear);
    for (int i = 0; i < 4; ++i) give(static_cast<int>(ItemDef::Class::Consumable), 0, 1, GiveTo::Bag);
    for (int i = 0; i < 4; ++i) give(static_cast<int>(ItemDef::Class::Consumable), 10000000, 1, GiveTo::Bag);
}

bool Game::give(int itemClass, int code, int count, GiveTo to, int wear) {
    if (itemClass < 0 || itemClass > 4) return false;
    const ItemDef* def = items_.find(static_cast<ItemDef::Class>(itemClass), code);
    if (!def) {
        log::warn("No item {}/{}", itemClass, code);
        return false;
    }
    Item item = Item::make(def, def->isGold() ? count : 1);
    item.wear = wear;
    if (to == GiveTo::Ground) {
        if (!player_) return false;
        // Side by side in a row below the hero.
        const double k = static_cast<double>(groundItems_.size() % 5) - 2;
        dropItem(item, player_->x + 160 + k * 100, player_->y + 160 - k * 100);
        groundItems_.back().waitForLeave = true;
        return true;
    }
    if (to == GiveTo::Wear) {
        const auto slot = inventory_.slotFor(*def);
        if (!slot || inventory_.canEquip(*slot, *def, playerLevel_) != Inventory::Result::Done) {
            log::warn("Cannot wear {}", def->name);
            return false;
        }
        if (auto old = inventory_.equip(*slot, item)) inventory_.add(*old);
        updateEquipment();
        return true;
    }
    if (!inventory_.add(item)) {
        log::warn("No room in the bag for {}", def->name);
        return false;
    }
    return true;
}

void Game::spawnGroundItems() {
    groundItems_.clear();
    int unknown = 0;
    for (const auto& p : control_.placements) {
        if (p.group != ScenarioControl::Group::Item) continue;
        const ItemDef* def = p.itemClass >= 0 && p.itemClass <= 4 ? items_.find(static_cast<ItemDef::Class>(p.itemClass), p.itemCode) : nullptr;
        if (!def) {
            ++unknown;
            continue;
        }
        dropItem(Item::make(def), p.x, p.y);
    }
    if (unknown > 0) log::warn("{} placed items are not in the item table", unknown);
}

void Game::dropItem(const Item& item, double x, double y) {
    GroundItem g{item, x, y, std::nullopt};
    // Folders 0-6 of Character/ITEM hold animations, 7-11 plain pictures.
    char stemBuf[64];
    std::snprintf(stemBuf, sizeof stemBuf, "Character/ITEM/%08d/Animation", item.def->groundFolder);
    const std::string stem = stemBuf;
    if (item.def->groundFolder >= 0 && (data_->exists(stem + ".caf") || data_->exists(stem + ".anim"))) {
        if (const CharacterModel* m = model(stem); m && item.def->groundPicture >= 0 &&
                                                   static_cast<std::size_t>(item.def->groundPicture) < m->animations.charts.size()) {
            Character c(m);
            c.x = x;
            c.y = y;
            c.restart(item.def->groundPicture);
            // Item charts use the ninth, "no direction" slot.
            const auto& dirs = m->animations.charts[static_cast<std::size_t>(item.def->groundPicture)].directions;
            for (std::size_t d = dirs.size(); d-- > 0;) {
                if (dirs[d].frameCount > 0) {
                    c.direction = static_cast<int>(d);
                    break;
                }
            }
            c.visibleLayers.clear();
            for (int i = 0; i < 32; ++i) c.visibleLayers.push_back(i);
            g.sprite = std::move(c);
        }
    }
    groundItems_.push_back(std::move(g));
}

void Game::dropLoot(const Actor& enemy) {
    const auto& stats = enemy.placement.stats;
    const double x = enemy.character.x, y = enemy.character.y;
    if (const int gold = rollGold(stats.goldChance, stats.goldMin, stats.goldMax, random_)) {
        if (const ItemDef* def = items_.find(ItemDef::Class::Special, 0)) {
            dropItem({def, gold}, x, y);
        } else {
            inventory_.addGold(gold);
        }
    }
    if (random_.next(100) < kItemDropChance) {
        if (const ItemDef* def = pickDrop(items_, stats.level, random_)) {
            dropItem(Item::make(def), x + 60, y + 60);
            log::info("A monster dropped {}", def->name);
        }
    }
}

void Game::pickUpItems() {
    for (auto it = groundItems_.begin(); it != groundItems_.end();) {
        const double d = std::hypot(it->x - player_->x, it->y - player_->y);
        if (d > kPickupRange) {
            it->waitForLeave = false;
            ++it;
            continue;
        }
        if (it->waitForLeave) {
            ++it;
            continue;
        }
        const Item item = it->item;
        if (!inventory_.add(item)) {
            showMessage(std::format("No room for {}", item.shownName()));
            it->waitForLeave = true;
            ++it;
            continue;
        }
        if (item.def->isGold()) {
            showMessage(std::format("{} gold", item.count));
        } else {
            showMessage(item.shownName());
        }
        log::info("Picked up {}{}", item.def->isGold() ? std::format("{} ", item.count) : std::string(), item.def->name);
        it = groundItems_.erase(it);
    }
}

void Game::updateEquipment() {
    if (player_) player_->visibleLayers = inventory_.heroLayers();
}

// As 0x44ea60 sums them: the hero's own stats, what they wear, their
// element levels, then Berserker while it is on.
HeroStats Game::playerStats() const {
    HeroStats stats = heroStats();
    const auto worn = inventory_.stats();
    auto& v = stats.value;
    v[HeroStats::kAttack] += worn[ItemDef::kAttack];
    v[HeroStats::kHit] += worn[ItemDef::kHit];
    v[HeroStats::kDefense] += worn[ItemDef::kDefense];
    v[HeroStats::kEvasion] += worn[ItemDef::kEvasion];
    v[HeroStats::kMagicAttack] += worn[ItemDef::kMagicAttack];
    v[HeroStats::kMagicHit] += worn[ItemDef::kMagicHit];
    v[HeroStats::kMagicDefense] += worn[ItemDef::kMagicDefense];
    v[HeroStats::kMagicEvasion] += worn[ItemDef::kMagicEvasion];
    v[HeroStats::kAttackSpeed] += worn[ItemDef::kAttackSpeed];
    v[HeroStats::kWalkSpeed] += worn[ItemDef::kWalkSpeed];
    int chant = 0;  // chant speed has no source yet besides elements
    applyElementLevels(stats, chant, playerElements());
    if (spellOn(kBerserker)) applyBerserker(stats, chant, magic_, spells_.castLevel(kBerserker));
    return stats;
}

// A new hero stands in the middle of the attribute disc, so their own
// levels are all 0; what they wear adds to that.
ElementLevels Game::playerElements() const { return elementLevels({}, inventory_.elements()); }

void Game::useItem(int index) {
    const ItemDef* def = inventory_.use(index);
    if (!def) return;
    // Effect words 1 and 2 restore health and mental power.
    if (def->effect[1] > 0) playerHp_ = std::min(playerMaxHp(), playerHp_ + def->effect[1]);
    if (def->effect[2] > 0) playerMp_ = std::min(playerMaxMp(), playerMp_ + def->effect[2]);
    showMessage(std::format("Used {}", def->name));
}

// The original's experience curve is in Table.Tbd; the open content uses ours.
void Game::loadLevelCurve() {
    if (auto raw = data_->read("System/Game/Parameter/Table.Tbd")) {
        try {
            tables_ = GameTables::parse(*raw);
            levels_ = LevelCurve::fromTables(tables_);
        } catch (const FormatError& e) {
            log::warn("Table.Tbd: {}", e.what());
        }
    }
    if (levels_.needs.empty()) levels_ = LevelCurve::standard();
    magic_ = tables_.tables.empty() ? MagicTables::standard() : MagicTables::fromTables(tables_);
}

void Game::gainExperience(int amount) {
    if (playerLevel_ >= levels_.maxLevel()) return;
    playerExperience_ += amount;
    if (playerExperience_ < levels_.need(playerLevel_)) return;
    // As in the original: one level at a time, counting again from 0.
    ++playerLevel_;
    playerExperience_ = 0;
    career_.levelUp(playerLevel_);
    const std::vector<int> learned = spells_.learn(magic_, career_.history);
    // A new level heals fully and refills magic, as in the original.
    playerHp_ = playerMaxHp();
    playerMp_ = playerMaxMp();
    log::info("Level {}, {}", playerLevel_, className(career_.current));
    std::string spells;
    for (const int spell : learned) {
        log::info("Learned {}", magic_.spell(spell).name);
        spells += std::format("{}{}", spells.empty() ? " Learned " : ", ", magic_.spell(spell).name);
    }
    // The level-up window's lines about the class (0x451771).
    if (playerLevel_ == 5) {
        showMessage(std::format("Level {}! Changed your occupation to [{}]{}", playerLevel_, className(career_.current), spells));
    } else if (career_.canChange()) {
        showMessage(std::format("Level {}! It is possible to change your occupation to [{}]{}", playerLevel_,
                                className(career_.offered), spells));
    } else {
        showMessage(std::format("Level {}!{}", playerLevel_, spells));
    }
}

void Game::showMessage(std::string text) {
    message_ = std::move(text);
    messageTicks_ = kMessageTicks;
}

void Game::dropHand() {
    if (!hand_ || !player_) return;
    dropItem(*hand_, player_->x, player_->y);
    groundItems_.back().waitForLeave = true;
    hand_.reset();
}

// Mouse clicks while the inventory is open or an item is in hand, in
// screen pixels. Returns whether the click was used up.
bool Game::inventoryClick(int x, int y, bool right) {
    if (!showInventory_ || !inside(kPanel, x, y)) {
        if (!hand_) return false;
        if (!right) dropHand();
        return true;
    }
    const SDL_Rect bag{kBagX, kBagY, Inventory::kBagWidth * kCell, Inventory::kBagHeight * kCell};
    if (inside(bag, x, y)) {
        const int cx = (x - kBagX) / kCell, cy = (y - kBagY) / kCell;
        if (right) {
            if (!hand_) useItem(inventory_.indexAt(cx, cy));
            return true;
        }
        if (hand_) {
            // The item is held by its middle; it goes in the cells under it.
            const double left = x - hand_->def->width * kCell / 2.0 - kBagX;
            const double top = y - hand_->def->height * kCell / 2.0 - kBagY;
            const int tx = static_cast<int>(std::lround(left / kCell)), ty = static_cast<int>(std::lround(top / kCell));
            if (inventory_.clickBag(hand_, tx, ty) == Inventory::Result::NoRoom) showMessage("No room there");
        } else {
            inventory_.clickBag(hand_, cx, cy);
        }
        return true;
    }
    for (const auto& box : kSlots) {
        if (!inside(box.rect(), x, y) || right) continue;
        switch (inventory_.clickSlot(hand_, box.slot, playerLevel_)) {
            case Inventory::Result::Done: updateEquipment(); break;
            case Inventory::Result::WrongSlot: showMessage("That does not go there"); break;
            case Inventory::Result::LevelTooLow: showMessage(std::format("Needs level {}", hand_->def->requiredLevel)); break;
            default: break;
        }
        return true;
    }
    return true;
}

std::optional<std::size_t> Game::groundItemAt(float screenX, float screenY) const {
    std::optional<std::size_t> best;
    double bestDistance = 28;
    for (std::size_t i = 0; i < groundItems_.size(); ++i) {
        int sx, sy;
        worldToScreen(static_cast<int>(groundItems_[i].x), static_cast<int>(groundItems_[i].y), sx, sy);
        const double d = std::hypot(sx - cameraX_ - screenX, sy - 8 - cameraY_ - screenY);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

// An original .njp sprite bank or an open content .sheet, by path stem.
const SpriteBank* Game::itemBank(const std::string& stem) {
    auto [it, inserted] = itemBanks_.try_emplace(stem);
    if (inserted) {
        try {
            if (auto njp = data_->read(stem + ".njp")) {
                it->second = std::make_unique<SpriteBank>(SpriteBank::parse(*njp));
            } else if (data_->exists(stem + ".sheet")) {
                if (auto sheet = loadSheet(*data_, stem + ".sheet")) it->second = std::make_unique<SpriteBank>(std::move(*sheet));
            }
        } catch (const FormatError& e) {
            log::warn("{}: {}", stem, e.what());
        }
    }
    return it->second.get();
}

// An item's inventory picture centred in a screen rectangle, or a plain
// box with its initial when there is none.
void Game::drawItemPicture(const ItemDef& def, const SDL_Rect& area) {
    const SpriteBank* bank = def.invBank >= 0 ? itemBank(std::format("System/Game/Pattern/Item{:04d}", def.invBank)) : nullptr;
    if (const SpriteTexture* sprite = bank ? sprites_.get(*bank, def.invPattern, false) : nullptr) {
        SDL_Rect dst{area.x + (area.w - sprite->width) / 2, area.y + (area.h - sprite->height) / 2, sprite->width, sprite->height};
        SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        return;
    }
    const SDL_Rect box{area.x + 3, area.y + 3, area.w - 6, area.h - 6};
    frame(renderer_, box, {90, 70, 40, 255}, {200, 170, 110, 255});
    drawText(renderer_, box.x + 3, box.y + 3, def.name.substr(0, 1), kWhite);
}

// The inventory panel's background: the open pack's Inventory.sheet, or
// the original's item window (Status.njp pattern 0, the figure behind the
// worn items, under pattern 2, the window, and pattern 3, its left part).
// Their positions are screen positions. False when there is neither.
bool Game::drawPanelPicture() {
    const SpriteBank* bank = itemBank("System/Game/Pattern/Inventory");
    std::vector<int> patterns = {0};
    if (!bank) {
        bank = itemBank("System/Game/Pattern/Status");
        patterns = {0, 2, 3};
    }
    bool drawn = false;
    for (int pattern : patterns) {
        if (const SpriteTexture* sprite = bank ? sprites_.get(*bank, pattern, false) : nullptr) {
            const SDL_Rect dst{sprite->originX, sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
            drawn = true;
        }
    }
    return drawn;
}

void Game::renderGroundItems(int camX, int camY) {
    std::vector<const GroundItem*> order;
    for (const auto& g : groundItems_) order.push_back(&g);
    std::ranges::stable_sort(order, [](const GroundItem* a, const GroundItem* b) { return a->x + a->y < b->x + b->y; });
    for (const GroundItem* g : order) {
        int sx, sy;
        worldToScreen(static_cast<int>(g->x), static_cast<int>(g->y), sx, sy);
        sx -= camX;
        sy -= camY;
        if (sx < -128 || sy < -128 || sx > kScreenWidth + 128 || sy > kScreenHeight + 128) continue;
        if (g->sprite) {
            g->sprite->draw(renderer_, sprites_, camX, camY);
            continue;
        }
        const ItemDef& def = *g->item.def;
        const SpriteBank* bank =
            def.groundFolder >= 0 ? itemBank(std::format("Character/ITEM/{:08d}/Pattern", def.groundFolder)) : nullptr;
        if (const SpriteTexture* sprite = bank ? sprites_.get(*bank, def.groundPicture, false) : nullptr) {
            SDL_Rect dst{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
            continue;
        }
        // No picture: a small diamond.
        const SDL_Color c = kGoldColor;
        const float x = static_cast<float>(sx), y = static_cast<float>(sy);
        const SDL_Vertex v[4] = {{{x, y - 6}, c, {}}, {{x + 9, y}, c, {}}, {{x, y + 6}, c, {}}, {{x - 9, y}, c, {}}};
        const int indices[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer_, nullptr, v, 4, indices, 6);
    }
}

void Game::renderInventory() {
    int mx = 0, my = 0;
    {
        int wx, wy;
        SDL_GetMouseState(&wx, &wy);
        float lx, ly;
        SDL_RenderWindowToLogical(renderer_, wx, wy, &lx, &ly);
        mx = static_cast<int>(lx);
        my = static_cast<int>(ly);
    }
    const Item* hover = nullptr;
    if (showInventory_) {
        // The panel's picture has the slots and bag cells drawn in; without
        // one, plain rectangles stand in.
        const bool picture = drawPanelPicture();
        if (!picture) frame(renderer_, kPanel, {20, 18, 26, 225}, {150, 130, 90, 255});
        // Equipment slots.
        for (const auto& box : kSlots) {
            const SDL_Rect r = box.rect();
            if (!picture) frame(renderer_, r, {45, 40, 50, 255}, {110, 100, 80, 255});
            if (const auto& worn = inventory_.equipped(box.slot)) {
                drawItemPicture(*worn->def, r);
                if (inside(r, mx, my)) hover = &*worn;
            }
        }
        // A shield shown but not drawn on the hero is greyed out.
        const auto& weapon = inventory_.equipped(EquipSlot::Weapon);
        if (inventory_.equipped(EquipSlot::Shield) && weapon && weapon->def->twoHanded()) {
            const SDL_Rect r = kSlots[4].rect();
            frame(renderer_, r, {0, 0, 0, 140}, {110, 100, 80, 255});
        }
        // The bag.
        if (!picture) {
            for (int y = 0; y < Inventory::kBagHeight; ++y) {
                for (int x = 0; x < Inventory::kBagWidth; ++x) {
                    frame(renderer_, {kBagX + x * kCell, kBagY + y * kCell, kCell, kCell}, {35, 32, 40, 255},
                          {80, 74, 64, 255});
                }
            }
        }
        for (const auto& p : inventory_.bag()) {
            const SDL_Rect r{kBagX + p.x * kCell, kBagY + p.y * kCell, p.item.def->width * kCell, p.item.def->height * kCell};
            // Over a picture, a faint wash shows which cells an item takes.
            if (picture) {
                frame(renderer_, r, {90, 80, 110, 40}, {150, 130, 90, 90});
            } else {
                frame(renderer_, r, {60, 55, 70, 255}, {120, 110, 90, 255});
            }
            drawItemPicture(*p.item.def, r);
            if (inside(r, mx, my)) hover = &p.item;
        }
        // Totals on the left.
        int ty = 20;
        for (const auto& line : {std::format("LV {}", playerLevel_), std::format("EXP {}/{}", playerExperience_, levels_.need(playerLevel_)),
                                 std::format("ATK {}", playerAttack()),
                                 std::format("DEF {}", playerDefense()), std::format("HIT {}", playerHit()),
                                 std::format("EVA {}", playerEvasion())}) {
            drawText(renderer_, 336, ty, line, kWhite);
            ty += 16;
        }
        drawText(renderer_, 336, kBagY + Inventory::kBagHeight * kCell + 10, std::format("GOLD {}", inventory_.gold()), kGoldColor);
        if (hover && !hand_) {
            const ItemDef& def = *hover->def;
            std::string text = hover->shownName();
            const auto& s = def.stats;
            // Unidentified items keep what they do to themselves.
            if (hover->unidentified) {
                text += "  ?";
            } else {
                if (s[ItemDef::kAttack]) text += std::format("  ATK {}", s[ItemDef::kAttack]);
                if (s[ItemDef::kDefense]) text += std::format("  DEF {}", s[ItemDef::kDefense]);
            }
            if (def.durability > 0) text += std::format("  DUR {}/{}", hover->durability(), def.durability);
            if (def.requiredLevel > 1) text += std::format("  LV {}", def.requiredLevel);
            if (def.effect[1] > 0) text += std::format("  +{} HP", def.effect[1]);
            if (text.size() > 40) text.resize(40);
            drawText(renderer_, 336, kBagY - 18, text, def.requiredLevel > playerLevel_ || hover->broken() ? kRed : kWhite);
        }
    }
    if (hand_) {
        const int w = hand_->def->width * kCell, h = hand_->def->height * kCell;
        drawItemPicture(*hand_->def, {mx - w / 2, my - h / 2, w, h});
    }
}

void Game::renderHud() {
    if (menuOpen_) return;
    renderSpellBar();
    if (player_ && !showInventory_) drawText(renderer_, 8, kScreenHeight - 46, std::format("GOLD {}", inventory_.gold()), kGoldColor);
    if (messageTicks_ > 0 && !message_.empty()) {
        drawText(renderer_, (kScreenWidth - textWidth(message_)) / 2, 40, message_, kWhite);
    }
}

}  // namespace sf
