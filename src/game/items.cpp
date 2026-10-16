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
#include "world/content.h"
#include "game/game.h"
#include "render/character_draw.h"
#include "platform/filesystem.h"
#include "render/tiny_font.h"

namespace sf {

namespace {

constexpr double kPickupRange = 150.0;  // world units from the player's feet
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

// The button at the bottom of the item window that opens the special
// items window in its place (0x446320, the same spot as its own button in
// special_items.cpp).
constexpr SDL_Rect kSpecialButton{376, 394, 67, 9};

// The Warehouse Transfer Box, the picture of a strongbox left of the
// accessories: an item put there goes to the town warehouse (0x447290
// answers 10 for it).
constexpr SDL_Rect kTransferBox{336, 140, 56, 72};
// The mine picture above it (0x447290 answers 11).
constexpr SDL_Rect kMineSlot{336, 88, 46, 46};

// The weapon set tabs I and II under the weapon slot and the shield slot
// (0x406aa0): drawn 21 x 7 at these places, answering clicks a pixel in
// from the left of tab I.
struct SetTab {
    int part, set;
    SDL_Rect drawn, hit;
};
constexpr SetTab kSetTabs[] = {
    {Inventory::kSetWeapon, 0, {490, 145, 21, 7}, {491, 145, 20, 7}},
    {Inventory::kSetWeapon, 1, {513, 145, 21, 7}, {513, 145, 21, 7}},
    {Inventory::kSetShield, 0, {490, 257, 21, 7}, {491, 257, 20, 7}},
    {Inventory::kSetShield, 1, {513, 257, 21, 7}, {513, 257, 21, 7}},
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
        // The original's unique items, with our names and pictures
        // (tools/content/uniques.py).
        if (auto uniques = data_->read("uniques.txt")) {
            ItemTable more;
            try {
                open::parseItems(*uniques, more);
                for (auto& def : more.items) {
                    if (items_.find(def.cls, def.code)) {
                        log::warn("uniques.txt: {} is in items.txt too; keeping that one", def.name);
                    } else {
                        items_.items.push_back(std::move(def));
                    }
                }
            } catch (const FormatError& e) {
                log::warn("uniques.txt: {}", e.what());
            }
        }
        if (auto drops = data_->read("drops.txt")) {
            try {
                dropTables_ = open::parseDrops(*drops);
            } catch (const FormatError& e) {
                log::warn("drops.txt: {}", e.what());
            }
        }
        if (auto shops = data_->read("shops.txt")) {
            try {
                openShops_ = open::parseShops(*shops);
            } catch (const FormatError& e) {
                log::warn("shops.txt: {}", e.what());
            }
        }
    }
    inventory_.setGoldDef(items_.find(ItemDef::Class::Special, 0));
    log::info("Loaded {} item definitions", items_.items.size());
}

// A new character's equipment (ShadowFlare.exe 0x44104c): Leather Cloth
// worn, and four Tablets and four Capsules both in the bag and in the belt,
// the Tablets along its top row and the Capsules along the bottom.
void Game::giveStartingKit() {
    mineCount_ = mines::kStartCount;
    give(static_cast<int>(ItemDef::Class::Armour), 0, 1, GiveTo::Wear);
    for (const std::int32_t code : {0, 10000000}) {
        for (int i = 0; i < 4; ++i) give(static_cast<int>(ItemDef::Class::Consumable), code, 1, GiveTo::Bag);
    }
    for (const std::int32_t code : {0, 10000000}) {
        const ItemDef* def = items_.find(ItemDef::Class::Consumable, code);
        for (int i = 0; def && i < 4; ++i) inventory_.addToBelt(Item::make(def, random_));
    }
}

bool Game::give(int itemClass, int code, int count, GiveTo to, int wear) {
    if (itemClass < 0 || itemClass > 4) return false;
    const ItemDef* def = items_.find(static_cast<ItemDef::Class>(itemClass), code);
    if (!def) {
        log::warn("No item {}/{}", itemClass, code);
        return false;
    }
    Item item = Item::make(def, random_, def->isGold() ? count : 1);
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
    if (def->isMine()) {
        if (pickUpMine()) return true;
        log::warn("No room for more mines");
        return false;
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
        dropItem(Item::make(def, random_), p.x, p.y);
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

// A monster's gold (0x45a030), dropped 100 units below it, then its drop
// list (0x45a000).
void Game::dropLoot(const Actor& enemy) {
    const auto& stats = enemy.placement.stats;
    const double x = enemy.character.x, y = enemy.character.y;
    int chance = stats.goldChance, least = stats.goldMin, most = stats.goldMax;
    // drops.txt can give gold to open content monsters that have none.
    if (const auto* list = dropTables_.list(stats.dropList); list && list->gold && chance <= 0) {
        chance = (*list->gold)[0];
        least = (*list->gold)[1];
        most = (*list->gold)[2];
    }
    if (int gold = rollGold(chance, least, most, inventory_.goldBonus(), random_)) {
        // Ours: the Debug tab's gold rate. Above a pile's 10,000 the rest
        // lands in more piles around the first.
        gold = scaleGold(gold, goldRate());
        log::info("Dropped {} gold", gold);
        if (const ItemDef* def = items_.find(ItemDef::Class::Special, 0)) {
            const auto piles = goldPiles(gold, Inventory::kGoldStack);
            for (std::size_t i = 0; i < piles.size(); ++i) {
                const double dx = i ? random_.next(200) - 100 : 0, dy = i ? random_.next(200) - 100 : 0;
                dropItem({def, piles[i]}, x + dx, y + 100 + dy);
            }
        } else {
            inventory_.addGold(gold);
        }
    }
    dropFromList(stats.dropList, x, y);
}

void Game::dropFromList(int list, double x, double y) {
    const auto drops = rollDrops(dropTables_, items_, list, random_, rules_.dropQualityPerRoll, luck(), playerLevel_);
    for (std::size_t i = 0; i < drops.size(); ++i) {
        // Spread around the spot by how many rolls the list has, as the
        // exe does, whether or not each roll gave something.
        const auto* l = dropTables_.list(list);
        const int rolls = l && l->rolls > 0 ? l->rolls : 1;
        double dx, dy;
        dropSpot(static_cast<int>(i), rolls, x, y, dx, dy);
        dropItem(Item::make(drops[i], random_, 1, luck()), dx, dy);
        log::info("Dropped {}", drops[i]->name);
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
        if (item.def->isMine()) {
            // Mines are counted, not put in the bag; with as many as can be
            // carried, they stay where they are (0x44a0e2).
            if (!pickUpMine()) {
                showMessage("No room for more mines");
                it->waitForLeave = true;
                ++it;
                continue;
            }
            showMessage(item.shownName());
            log::info("Picked up a mine, {} now", mineCount_);
            it = groundItems_.erase(it);
            continue;
        }
        if (item.def->specialPage >= 0) {
            // Onto its place in the special items window, with its take
            // sound; when one like it is there already it stays on the
            // ground, without a word (0x449ef0).
            if (!inventory_.addSpecial(item)) {
                it->waitForLeave = true;
                ++it;
                continue;
            }
            playSound(itemSound(*item.def, kItemTake));
            showMessage(item.shownName());
            log::info("Picked up {}", item.def->name);
            it = groundItems_.erase(it);
            continue;
        }
        if (!inventory_.pickUp(item)) {
            showMessage(std::format("No room for {}", item.shownName()));
            it->waitForLeave = true;
            ++it;
            continue;
        }
        if (item.def->isGold()) {
            showMessage(std::format("{} gold", item.count));
        } else {
            playSound(0x30);  // a fixed sound, into the belt or the bag; gold is silent (0x449ef0)
            showMessage(item.shownName());
        }
        log::info("Picked up {}{}", item.def->isGold() ? std::format("{} ", item.count) : std::string(), item.def->name);
        it = groundItems_.erase(it);
    }
}

void Game::updateEquipment() {
    if (player_) {
        player_->visibleLayers = inventory_.heroLayers();
        player_->layerTints.clear();
        for (const auto& c : inventory_.heroColours()) player_->layerTints.push_back({c.layer, c.rgb[0], c.rgb[1], c.rgb[2]});
    }
    // Taking off what raised the most leaves only that many (0x450183).
    mineCount_ = std::min(mineCount_, mostMines());
    // What the hero wears can raise the companion's stats.
    refreshPartnerStats();
}

// As 0x44ea60 sums them: the hero's own stats, what they wear and its
// modifiers, their element levels, then Berserker while it is on.
HeroStats Game::playerStats() const {
    int chant = 0;
    return playerStats(chant);
}

int Game::playerChantSpeed() const {
    int chant = 0;
    playerStats(chant);
    return chant;
}

HeroStats Game::playerStats(int& chant) const {
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
    v[HeroStats::kHp] += inventory_.modifier(ItemDef::kModMaxHp);
    v[HeroStats::kMp] += inventory_.modifier(ItemDef::kModMaxMp);
    v[HeroStats::kStrength] += inventory_.modifier(ItemDef::kModStrength);
    chant = kHeroChantSpeed + inventory_.modifier(ItemDef::kModChantSpeed);
    applyElementLevels(stats, chant, playerElements());
    if (spellOn(kBerserker)) applyBerserker(stats, chant, magic_, castLevel(kBerserker));
    return stats;
}

// A new hero stands in the middle of the attribute disc, so their own
// levels are all 0; what they wear adds to that.
ElementLevels Game::playerElements() const { return elementLevels(discElementLevels(discX_, discY_), inventory_.elements()); }

// Using a consumable (0x44a240). Effect words 1 and 3 restore health (3 in
// percent of the most), 2 and 4 mental power, raised by the stamina and
// mental medicine modifiers; only what is not full takes them. When the
// hero takes nothing, words 5 and 6 heal the companion likewise (meat),
// while it is up and hurt. When that does nothing either, word 7 moves the
// hero on the attribute disc (the medicines): -2 back to the middle (White
// Medicine), or word 8 units towards element 7's anchor (0x44fd10).
// Something that does nothing is not used up.
bool Game::applyConsumable(const ItemDef& def) {
    if (def.cls != ItemDef::Class::Consumable) return false;
    const auto& e = def.effect;
    bool used = false;
    auto restore = [&](int& value, int max, int amount, int percent, int bonus) {
        if (value == max) return;
        const int before = value;
        value += amount * (bonus + 100) / 100;
        value += percent * max / 100 * (bonus + 100) / 100;
        value = std::min(value, max);
        used = used || value != before;
    };
    restore(playerHp_, playerMaxHp(), e[1], e[3], inventory_.modifier(ItemDef::kModStaminaMedicine));
    restore(playerMp_, playerMaxMp(), e[2], e[4], inventory_.modifier(ItemDef::kModMentalMedicine));
    if (!used && partnerAlive() && partner_->hp > 0) {
        Partner& p = *partner_;
        const int before = p.hp;
        if (p.hp != p.stats.maxHp) {
            p.hp += e[5];
            p.hp += e[6] * p.stats.maxHp / 100;
            p.hp = std::min(p.hp, p.stats.maxHp);
        }
        used = p.hp != before;
    }
    if (!used && e[7] != -1) {
        const int x = discX_, y = discY_;
        if (e[7] == -2) {
            discX_ = discY_ = 0;
        } else {
            moveOnDisc(discX_, discY_, e[7], e[8]);
        }
        used = discX_ != x || discY_ != y;
        if (used) updateEquipment();
    }
    if (!used) return false;
    playSound(0x10);  // (0x44a240)
    showMessage(std::format("Used {}", def.name));
    return true;
}

void Game::useItem(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= inventory_.bag().size()) return;
    if (applyConsumable(*inventory_.bag()[static_cast<std::size_t>(index)].item.def)) inventory_.use(index);
}

// The original's experience curve is in Table.Tbd; the open content uses ours.
void Game::loadLevelCurve() {
    if (auto raw = data_->read("System/Game/Parameter/Table.Tbd")) {
        try {
            tables_ = GameTables::parse(*raw);
            levels_ = LevelCurve::fromTables(tables_);
            modifierPrices_ = ModifierPrices::load(tables_);
            dropTables_ = DropTables::fromTables(tables_);
        } catch (const FormatError& e) {
            log::warn("Table.Tbd: {}", e.what());
        }
    }
    if (levels_.needs.empty()) levels_ = LevelCurve::standard();
    magic_ = tables_.tables.empty() ? MagicTables::standard() : MagicTables::fromTables(tables_);
    partnerTables_ = PartnerTables::load(tables_);
    if (tables_.tables.empty()) {
        // An open pack's companions.
        if (auto text = data_->read("partners.txt")) {
            try {
                partnerTables_ = PartnerTables::load(open::parsePartners(*text));
            } catch (const FormatError& e) {
                log::warn("partners.txt: {}", e.what());
            }
        }
    } else if (!partnerTables_) {
        log::warn("No partner tables (60, 800-805); playing without a companion");
    }
}

void Game::gainExperience(int amount) {
    if (playerLevel_ >= levels_.maxLevel()) return;
    // The Debug tab's experience rate; original mode plays at 1x.
    const int rate = xpRate();
    if (rate != Settings::kXpRateNormal && amount > 0) {
        amount = std::max(1, static_cast<int>(static_cast<std::int64_t>(amount) * rate / 100));
    }
    playerExperience_ += amount;
    // The original goes up one level at a time and counts again from 0,
    // dropping whatever was over. With the rate raised that would throw
    // the extra away (a 100% quest reward at 2.0x would still be one
    // level), so then what is over carries on into the next levels.
    const bool carryOver = rate > Settings::kXpRateNormal;
    while (playerLevel_ < levels_.maxLevel() && playerExperience_ >= levels_.need(playerLevel_)) {
        playerExperience_ = carryOver ? playerExperience_ - levels_.need(playerLevel_) : 0;
        levelUp();
        if (!carryOver) break;
    }
}

void Game::levelUp() {
    ++playerLevel_;
    // Level 5 sets the first class, with its own sound first (0x412fb0).
    if (player_ && playerLevel_ == 5) playSound(0x40, player_->x, player_->y);
    if (player_) playSound(0x3f, player_->x, player_->y);
    career_.levelUp(playerLevel_);
    const std::vector<int> learned = spells_.learn(magic_, career_.history);
    // A new level heals fully and refills magic, as in the original.
    playerHp_ = playerMaxHp();
    playerMp_ = playerMaxMp();
    log::info("Level {}, {}", playerLevel_, className(career_.current, heroSex_));
    std::string spells;
    for (const int spell : learned) {
        log::info("Learned {}", magic_.spell(spell).name);
        spells += std::format("{}{}", spells.empty() ? " Learned " : ", ", magic_.spell(spell).name);
    }
    // The level-up window's lines about the class (0x451771).
    if (playerLevel_ == 5) {
        showMessage(std::format("Level {}! Changed your occupation to [{}]{}", playerLevel_, className(career_.current, heroSex_), spells));
    } else if (career_.canChange()) {
        showMessage(std::format("Level {}! It is possible to change your occupation to [{}]{}", playerLevel_,
                                className(career_.offered, heroSex_), spells));
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
    if (identifying_ && showInventory_ && inside(kPanel, x, y)) {
        // Only an unknown item in the bag can be chosen, with an empty
        // hand; that ends the spell and trains it (0x446320).
        const SDL_Rect bag{kBagX, kBagY, Inventory::kBagWidth * kCell, Inventory::kBagHeight * kCell};
        if (right) {
            applyItemRequest(ItemRequest::of(ItemRequest::Kind::CancelIdentify));
            return true;
        }
        if (hand_ || !inside(bag, x, y)) return true;
        applyItemRequest(ItemRequest::at(ItemRequest::Kind::Identify, (x - kBagX) / kCell, (y - kBagY) / kCell));
        return true;
    }
    if (!showInventory_ || !inside(kPanel, x, y)) {
        if (!hand_) return false;
        if (!right) applyItemRequest(ItemRequest::of(ItemRequest::Kind::DropHand));
        return true;
    }
    // The tabs answer first, whatever is in the hand.
    if (weaponSetTabClick(x, y, right)) return true;
    const SDL_Rect bag{kBagX, kBagY, Inventory::kBagWidth * kCell, Inventory::kBagHeight * kCell};
    if (inside(bag, x, y)) {
        const int cx = (x - kBagX) / kCell, cy = (y - kBagY) / kCell;
        if (right) {
            if (hand_) return true;
            const int index = inventory_.indexAt(cx, cy);
            if (index >= 0 && inventory_.bag()[static_cast<std::size_t>(index)].item.def->isGold()) {
                const auto& placed = inventory_.bag()[static_cast<std::size_t>(index)];
                openGoldSplit(GoldSplit::From::Bag, placed.x, placed.y, x, y);
            } else if (rules_.rightClickStores && storageOpen() && index >= 0) {
                applyItemRequest(ItemRequest::at(ItemRequest::Kind::StoreFromBag, cx, cy));
            } else if (rules_.rightClickSells && openShop_ && !storageOpen()) {
                applyItemRequest(ItemRequest::at(ItemRequest::Kind::SellFromBag, cx, cy));
            } else if (rules_.rightClickEquips && index >= 0 &&
                       inventory_.slotFor(*inventory_.bag()[static_cast<std::size_t>(index)].item.def)) {
                applyItemRequest(ItemRequest::at(ItemRequest::Kind::EquipFromBag, cx, cy));
            } else {
                applyItemRequest(ItemRequest::at(ItemRequest::Kind::UseBag, cx, cy));
            }
            return true;
        }
        if (hand_) {
            // The item is held by its middle; it goes in the cells under it.
            const double left = x - hand_->def->width * kCell / 2.0 - kBagX;
            const double top = y - hand_->def->height * kCell / 2.0 - kBagY;
            applyItemRequest(ItemRequest::bagClick(static_cast<int>(std::lround(left / kCell)), static_cast<int>(std::lround(top / kCell))));
        } else {
            applyItemRequest(ItemRequest::bagClick(cx, cy));
        }
        return true;
    }
    if (inside(kTransferBox, x, y)) {
        if (!right) applyItemRequest(ItemRequest::of(ItemRequest::Kind::ToWarehouse));
        return true;
    }
    if (inside(kMineSlot, x, y)) {
        if (!right) applyItemRequest(ItemRequest::of(ItemRequest::Kind::MineSlot));
        return true;
    }
    if (!right && specialButtonLit(x, y)) {
        playSound(0x3a);
        openSpecialWindow();
        return true;
    }
    for (const auto& box : kSlots) {
        if (!inside(box.rect(), x, y)) continue;
        if (right) {
            applyItemRequest(ItemRequest::of(ItemRequest::Kind::UnequipToBag, static_cast<int>(box.slot)));
            return true;
        }
        applyItemRequest(ItemRequest::of(ItemRequest::Kind::SlotClick, static_cast<int>(box.slot)));
        return true;
    }
    return true;
}

bool Game::weaponSetTabClick(int x, int y, bool right) {
    for (const auto& tab : kSetTabs) {
        if (!inside(tab.hit, x, y)) continue;
        if (right) {
            applyItemRequest(ItemRequest::at(ItemRequest::Kind::ToggleShared, tab.part, tab.set));
        } else {
            applyItemRequest(ItemRequest::of(ItemRequest::Kind::ChooseWeaponSet, tab.set));
        }
        return true;
    }
    return false;
}

// Each tab: red while its set is in use, blue otherwise, and a bar in place
// of its number while that set's slot is shared (Status.njp patterns
// 0x5f-0x64, or the open pack's Tabs.sheet frames 22-27).
void Game::renderWeaponSetTabs() {
    const SpriteBank* open = itemBank("System/Game/Pattern/Tabs");
    const SpriteBank* bank = open ? open : itemBank("System/Game/Pattern/Status");
    for (const auto& tab : kSetTabs) {
        const bool inUse = inventory_.weaponSet() == tab.set;
        const bool shared = inventory_.shared(tab.part, tab.set);
        const int pattern = (inUse ? 0x5f : 0x62) + (shared ? 2 : tab.set);
        const SpriteTexture* sprite = bank ? sprites_.get(*bank, open ? pattern - 0x5f + 22 : pattern, false) : nullptr;
        if (sprite) {
            const SDL_Rect dst{tab.drawn.x + sprite->originX, tab.drawn.y + sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
        } else {
            frame(renderer_, tab.drawn, inUse ? SDL_Color{150, 60, 40, 255} : SDL_Color{60, 60, 110, 255}, {200, 180, 130, 255});
        }
    }
}

void Game::mineSlotClick() {
    if (!hand_) {
        const ItemDef* mine = items_.find(ItemDef::Class::Special, 1);
        if (mineCount_ <= 0 || !mine) return;
        hand_ = Item::make(mine);
        --mineCount_;
        return;
    }
    if (!hand_->def->isMine() || mineCount_ >= mostMines()) return;
    playSound(itemSound(*hand_->def, kItemPut));
    hand_.reset();
    ++mineCount_;
}

void Game::openGoldSplit(GoldSplit::From from, int cellX, int cellY, int mouseX, int mouseY) {
    if (hand_) return;
    int amount = 0;
    if (from == GoldSplit::From::Bag) {
        const int i = inventory_.indexAt(cellX, cellY);
        if (i >= 0) amount = inventory_.bag()[static_cast<std::size_t>(i)].item.count;
    } else {
        const ShopStock& stock = from == GoldSplit::From::Storage ? shops_[kStorage] : openStock();
        const int i = stock.indexAt(cellX, cellY);
        if (i >= 0) amount = stock.goods[static_cast<std::size_t>(i)].item.count;
    }
    goldSplit_ = GoldSplit{from, cellX, cellY, mouseX - 24, mouseY - 24, std::to_string(amount), true};
    SDL_StartTextInput();
}

// Digits, Backspace, Enter to take the gold and Esc or a click to leave it.
bool Game::goldSplitEvent(const SDL_Event& event) {
    if (!goldSplit_) return false;
    auto& box = *goldSplit_;
    switch (event.type) {
        case SDL_TEXTINPUT:
            for (const char* c = event.text.text; *c; ++c) {
                if (*c < '0' || *c > '9') continue;
                if (box.fresh) box.text.clear();
                box.fresh = false;
                if (box.text.size() < 6) box.text += *c;
            }
            return true;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_BACKSPACE:
                    box.fresh = false;
                    if (!box.text.empty()) box.text.pop_back();
                    break;
                case SDLK_RETURN:
                case SDLK_KP_ENTER: finishGoldSplit(); break;
                case SDLK_ESCAPE:
                    goldSplit_.reset();
                    SDL_StopTextInput();
                    break;
                default: break;
            }
            return true;
        case SDL_MOUSEBUTTONDOWN:
            goldSplit_.reset();
            SDL_StopTextInput();
            return true;
        case SDL_KEYUP:
        case SDL_MOUSEBUTTONUP: return true;
        default: return false;
    }
}

void Game::finishGoldSplit() {
    const GoldSplit box = *goldSplit_;
    goldSplit_.reset();
    SDL_StopTextInput();
    if (box.text.empty() || box.text.size() > 6) return;
    const int amount = std::stoi(box.text);
    if (amount <= 0) return;
    const auto kind = box.from == GoldSplit::From::Bag ? ItemRequest::Kind::SplitGold : ItemRequest::Kind::SplitStorageGold;
    applyItemRequest({kind, static_cast<std::int16_t>(box.x), static_cast<std::int16_t>(box.y), amount});
}

// A small box where the right click was, the amount in it (0x441ab0).
void Game::renderGoldSplit() {
    if (!goldSplit_) return;
    const SDL_Rect r{goldSplit_->boxX, goldSplit_->boxY, 48, 12 + 8};
    frame(renderer_, r, {0, 0, 0, 230}, {224, 224, 224, 255});
    const std::string& text = goldSplit_->text;
    if (goldSplit_->fresh) frame(renderer_, {r.x + 3, r.y + 3, uiTextWidth(text) + 2, 12 + 2}, {64, 64, 160, 255}, {64, 64, 160, 255});
    uiText(r.x + 4, r.y + 5, text, kWhite);
    // The caret blinks.
    if (!goldSplit_->fresh && (ticks_ / (kTicksPerSecond / 2)) % 2 == 0) {
        const int cx = r.x + 5 + uiTextWidth(text);
        frame(renderer_, {cx, r.y + 4, 1, 12}, kWhite, kWhite);
    }
}

// A right click on a weapon, armour or accessory in the bag wears it, and
// one on a worn item takes it off (ours, Rules::rightClickEquips).
void Game::equipFromBag(int index) {
    const ItemDef& def = *inventory_.bag()[static_cast<std::size_t>(index)].item.def;
    switch (inventory_.equipFromBag(index, playerLevel_)) {
        case Inventory::Result::Done:
            playSound(itemSound(def, kItemEquip));
            updateEquipment();
            break;
        case Inventory::Result::NoRoom: showMessage("No room in the bag"); break;
        case Inventory::Result::LevelTooLow: showMessage(std::format("Needs level {}", def.requiredLevel)); break;
        default: break;
    }
}

void Game::unequipToBag(EquipSlot slot) {
    const auto& worn = inventory_.equipped(slot);
    if (!worn) return;
    const ItemDef& def = *worn->def;
    switch (inventory_.unequipToBag(slot)) {
        case Inventory::Result::Done:
            playSound(itemSound(def, kItemPut));
            updateEquipment();
            break;
        case Inventory::Result::NoRoom: showMessage("No room in the bag"); break;
        default: break;
    }
}

// An item let go over the Warehouse Transfer Box goes to the warehouse,
// wherever the hero is, if there is room (0x446320). Items kept on a
// special items page go back there instead, unless one like it is there
// already, and never to the warehouse.
void Game::putInWarehouse() {
    if (!hand_) return;
    if (hand_->def->specialPage >= 0) {
        if (inventory_.addSpecial(*hand_)) hand_.reset();
        return;
    }
    const ItemDef& held = *hand_->def;
    if (shops_[kStorage].takeIn(hand_, !rules_.noEmptyGoldInWarehouse)) {
        playSound(itemSound(held, kItemPut));
    } else if (hand_) {
        // The original says nothing and keeps the item in the hand.
        showMessage("The warehouse is full");
    }
}

std::optional<std::size_t> Game::groundItemAt(float screenX, float screenY) const {
    std::optional<std::size_t> best;
    double bestDistance = INFINITY;
    for (std::size_t i = 0; i < groundItems_.size(); ++i) {
        int sx, sy;
        worldToScreen(static_cast<int>(groundItems_[i].x), static_cast<int>(groundItems_[i].y), sx, sy);
        const SDL_FPoint at = onScreen(sx, sy - 8);
        const double dx = at.x - screenX, dy = at.y - screenY;
        const double d = std::hypot(dx, dy);
        if (inClickRange(dx, dy) && d < bestDistance) {
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
                // The props sheet (scenario objects in the open pack) is
                // trimmed, as the map's own copy is: it is mostly see-through.
                if (auto sheet = loadSheet(*data_, stem + ".sheet", stem == "Map/props")) {
                    it->second = std::make_unique<SpriteBank>(std::move(*sheet));
                }
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

// The marks on an item's picture (0x465cb0), in the bottom right corner:
// a question mark while it is not identified, and above that a crack once
// it is broken, blinking while less than a tenth of its durability is
// left. The open pack's ItemMarks.sheet, or the original's Status.njp
// patterns 0x11 and 0x10.
void Game::drawItemMarks(const Item& item, const SDL_Rect& area) {
    const SpriteBank* open = itemBank("System/Game/Pattern/ItemMarks");
    const SpriteBank* bank = open ? open : itemBank("System/Game/Pattern/Status");
    auto mark = [&](int originalPattern, int openPattern, int x, int y) {
        if (const SpriteTexture* t = bank ? sprites_.get(*bank, open ? openPattern : originalPattern, false) : nullptr) {
            const SDL_Rect dst{x + t->originX, y + t->originY, t->width, t->height};
            SDL_RenderCopy(renderer_, t->texture, nullptr, &dst);
        }
    };
    int x = area.x + area.w - 16, y = area.y + area.h - 16;
    if (item.unidentified) {
        mark(0x11, 0, x, y);
        y -= 15;
    }
    const auto& def = *item.def;
    if ((def.cls != ItemDef::Class::Weapon && def.cls != ItemDef::Class::Armour) || def.durability <= 0) return;
    const int left = item.durability();
    // Eight of every sixteen of the original's ticks.
    const bool blinkOn = static_cast<std::uint64_t>(static_cast<double>(ticks_) * kOriginalTicksPerTick) % 16 < 8;
    if (left == 0 || (left * 100 / def.durability <= 9 && blinkOn)) mark(0x10, 1, x, y);
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
    std::vector<GroundItem*> order;
    for (auto& g : groundItems_) order.push_back(&g);
    std::ranges::stable_sort(order, [](const GroundItem* a, const GroundItem* b) { return a->x + a->y < b->x + b->y; });
    for (GroundItem* g : order) {
        // The one under the pointer is drawn brighter (0x40ee70).
        const int glow = hovered(Settings::kItem, static_cast<std::size_t>(g - groundItems_.data())) ? kHoverGlow : 0;
        int sx, sy;
        worldToScreen(static_cast<int>(g->x), static_cast<int>(g->y), sx, sy);
        sx -= camX;
        sy -= camY;
        if (sx < -128 || sy < -128 || sx > viewWidth() + 128 || sy > viewHeight() + 128) continue;
        if (g->sprite) {
            g->sprite->glow = glow;
            drawCharacter(renderer_, sprites_, *g->sprite, camX, camY);
            continue;
        }
        const ItemDef& def = *g->item.def;
        const SpriteBank* bank =
            def.groundFolder >= 0 ? itemBank(std::format("Character/ITEM/{:08d}/Pattern", def.groundFolder)) : nullptr;
        if (const SpriteTexture* sprite = bank ? sprites_.get(*bank, def.groundPicture, false) : nullptr) {
            if (glow > 0 && rules_.outlineUnderPointer) drawOutline(*bank, def.groundPicture, sx, sy, kHoverOutline);
            SDL_Rect dst{sx + sprite->originX, sy + sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
            if (glow > 0) {
                // A second copy added on top brightens it.
                SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_ADD);
                SDL_SetTextureAlphaMod(sprite->texture, static_cast<Uint8>(glow * 255 / 1000));
                SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
                SDL_SetTextureBlendMode(sprite->texture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(sprite->texture, 255);
            }
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

namespace {

// The colours of Game::Tone.
constexpr SDL_Color kTones[] = {{224, 224, 224, 255}, {224, 64, 64, 255}, {224, 192, 128, 255}};
constexpr SDL_Color kShadow{0, 0, 0, 255};


// The weight gauge: 33 marks four pixels apart from (338, 239), tan up to
// x = 437, darker to 457 and red after that (0x4074e0).
constexpr int kGaugeMarks = 33, kGaugeX = 338, kGaugeY = 239, kGaugeStep = 4;
constexpr int kTransferGlowSteps = 10;
constexpr SDL_Color kGaugeColours[] = {{214, 182, 134, 255}, {176, 144, 90, 255}, {168, 20, 20, 255}, {255, 255, 255, 255}};

}  // namespace

// Text as the item and status windows write it: in the original's second
// font (Font01, with lowercase; the open pack's Font00 stands in when it
// has none) over a black copy `shadow` pixels down and to the right.
void Game::windowText(int x, int y, std::string_view text, SDL_Color color, int shadow) {
    const SpriteBank* bank = itemBank("System/Common/Pattern/Font01");
    const SpriteTexture* font = bank && !bank->patterns.empty() ? sprites_.get(*bank, 0, false) : nullptr;
    if (!font || font->width < 96 || font->height < 192) {
        uiText(x + shadow, y + shadow, text, kShadow);
        uiText(x, y, text, color);
        return;
    }
    fontText(*bank, *font, x + shadow, y + shadow, text, kShadow);
    fontText(*bank, *font, x, y, text, color);
}

// A number as the item window writes it (0x405590): its last digit ends at
// x, eight pixels a digit, two pixels below y.
void Game::windowNumber(int x, int y, int value, int tone) {
    value = std::max(value, 0);
    do {
        x -= 8;
        windowText(x, y + 2, std::string(1, static_cast<char>('0' + value % 10)), kTones[tone]);
        value /= 10;
    } while (value != 0);
}

// The top left of the item window, as ShadowFlare.exe fills it (0x408a80
// and 0x4074e0): the gold, the mines' power and stock with a mine in the
// socket while any are carried, and what the hero wears weighed against
// their strength, as a number and a gauge. The labels POWER, STOCK, WEIGHT
// and OVER are part of the window's picture.
void Game::renderItemTotals() {
    const SpriteBank* open = itemBank("System/Game/Pattern/Inventory");
    const SpriteBank* bank = open ? open : itemBank("System/Game/Pattern/Status");
    // The original's Status.njp patterns, or the open pack's Inventory frames.
    auto part = [&](int originalPattern, int openPattern) -> const SpriteTexture* {
        return bank ? sprites_.get(*bank, open ? openPattern : originalPattern, false) : nullptr;
    };
    const bool labelled = drawnPanel_;
    if (!labelled) {
        drawText(renderer_, 342, 84, "POWER", kWhite, 1);
        drawText(renderer_, 342, 120, "STOCK", kWhite, 1);
        drawText(renderer_, 342, 228, "WEIGHT", kWhite, 1);
    }

    windowText(342, 30, "Total Gold", kTones[kRaised]);
    windowNumber(471, 28, inventory_.gold(), kPlain);

    if (mineCount_ > 0) {
        if (const SpriteTexture* mine = part(0x43, 1)) {
            const SDL_Rect dst{mine->originX, mine->originY, mine->width, mine->height};
            SDL_RenderCopy(renderer_, mine->texture, nullptr, &dst);
        }
    }
    const int mineBonus = inventory_.modifier(ItemDef::kModMineEffect);
    windowNumber(471, 80, minePower(playerLevel_), mineBonus > 0 ? kRaised : mineBonus < 0 ? kLowered : kPlain);
    const Tone stock = toneOf(mostMines(), mines::most(0));
    windowNumber(446, 116, mineCount_, stock);
    windowText(445, 117, "/", kTones[kPlain]);
    windowNumber(471, 116, mostMines(), stock);

    // Holding an item over the Warehouse Transfer Box lights it up, fading
    // in over ten steps and out again when it leaves (0x4074e0).
    if (transferGlowTick_ != ticks_) {
        transferGlowTick_ = ticks_;
        const auto at = pointerPosition();
        const bool over = hand_ && at && inside(kTransferBox, static_cast<int>(at->x), static_cast<int>(at->y));
        transferGlow_ = std::clamp(transferGlow_ + (over ? 1 : -1), 0, kTransferGlowSteps);
    }
    if (transferGlow_ > 0) {
        const Uint8 a = static_cast<Uint8>(transferGlow_ * 255 / kTransferGlowSteps);
        if (const SpriteTexture* glow = part(4, 6)) {
            SDL_SetTextureAlphaMod(glow->texture, a);
            const SDL_Rect dst{glow->originX, glow->originY, glow->width, glow->height};
            SDL_RenderCopy(renderer_, glow->texture, nullptr, &dst);
            SDL_SetTextureAlphaMod(glow->texture, 255);
        } else {
            frame(renderer_, kTransferBox, {255, 230, 160, static_cast<Uint8>(a / 4)}, {255, 230, 160, a});
        }
    }
    if (!labelled) {
        frame(renderer_, kTransferBox, {0, 0, 0, 0}, {150, 130, 90, 255});
        drawText(renderer_, kTransferBox.x + 4, kTransferBox.y + 32, "STORE", kWhite, 1);
    }

    const int strength = playerStats()[HeroStats::kStrength];
    const int weight = inventory_.wornWeight();
    windowNumber(471, 224, weight, weight > strength ? kLowered : kPlain);

    // The gauge eases to the new weight a mark a tick, the marks between
    // fading out, and flashes white the moment it starts to move.
    const int target = std::min(kGaugeMarks, strength < 1 ? kGaugeMarks : weight * kGaugeMarks / strength);
    if (weightGaugeShown_ < 0) weightGaugeShown_ = target;
    int alpha = 1000;
    for (int i = 0; i < weightGaugeShown_ || i < target; ++i) {
        const int x = kGaugeX + i * kGaugeStep;
        int kind = x < 438 ? 0 : x < 458 ? 1 : 2;
        if (target < weightGaugeShown_ && i > target) alpha -= 1000 / (weightGaugeShown_ - target);
        if (weightGaugeShown_ < target && i > weightGaugeShown_) alpha -= 1000 / (target - weightGaugeShown_);
        if (weightGaugeSettled_ && target != weightGaugeShown_ && alpha == 1000) kind = 3;
        const Uint8 a = static_cast<Uint8>(std::clamp(alpha, 0, 1000) * 255 / 1000);
        if (const SpriteTexture* mark = part(18 + kind, 2 + kind)) {
            SDL_SetTextureAlphaMod(mark->texture, a);
            const SDL_Rect dst{x, kGaugeY, mark->width, mark->height};
            SDL_RenderCopy(renderer_, mark->texture, nullptr, &dst);
            SDL_SetTextureAlphaMod(mark->texture, 255);
        } else {
            SDL_Color c = kGaugeColours[kind];
            c.a = a;
            frame(renderer_, {x, kGaugeY, 3, 5}, c, c);
        }
    }
    if (weightGaugeTick_ != ticks_) {
        weightGaugeTick_ = ticks_;
        weightGaugeSettled_ = weightGaugeShown_ == target;
        if (target < weightGaugeShown_) --weightGaugeShown_;
        else if (target > weightGaugeShown_) ++weightGaugeShown_;
    }
}

// The item window's button to the special items window: Status.njp pattern
// 0x74, or 0x75 lit under the pointer, but never while trading (0x4039f0);
// the open pack has it in the panel and lights it with Inventory frame 7.
void Game::renderSpecialButton(int mx, int my) {
    const bool lit = specialButtonLit(mx, my);
    const SpriteBank* open = itemBank("System/Game/Pattern/Inventory");
    if (open) {
        if (!lit) return;
        if (const SpriteTexture* sprite = sprites_.get(*open, 7, false)) {
            const SDL_Rect dst{sprite->originX, sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
            return;
        }
    } else if (const SpriteBank* bank = itemBank("System/Game/Pattern/Status")) {
        if (const SpriteTexture* sprite = sprites_.get(*bank, lit ? 0x75 : 0x74, false)) {
            const SDL_Rect dst{sprite->originX, sprite->originY, sprite->width, sprite->height};
            SDL_RenderCopy(renderer_, sprite->texture, nullptr, &dst);
            return;
        }
    }
    frame(renderer_, kSpecialButton, lit ? SDL_Color{90, 80, 50, 255} : SDL_Color{45, 40, 30, 255}, {150, 130, 90, 255});
    drawText(renderer_, kSpecialButton.x + 12, kSpecialButton.y + 1, "SPECIAL", lit ? SDL_Color{255, 220, 120, 255} : kWhite, 1);
}

// What the item under the mouse is, in a box centred under the pointer and
// kept on screen, edged in half-bright white, as the original shows it
// (0x409160). Ours marks an item too high a level or broken in red on the
// stats line.
void Game::renderItemPopup(const Item& item, int mx, int my) {
    const ItemDef& def = *item.def;
    std::vector<std::pair<std::string, SDL_Color>> lines;
    const bool unusable = def.requiredLevel > playerLevel_ || item.broken();
    const std::string name = def.isGold() ? std::format("{} {}", item.count, item.shownName()) : item.shownName();
    // The name in its quality's colour, as the original's box draws it, even
    // before the item is identified.
    lines.emplace_back(name + (item.unidentified ? "  ?" : ""), qualityColor(def.quality));
    std::string stats;
    const auto& s = def.stats;
    // Unidentified items keep what they do to themselves.
    if (!item.unidentified) {
        if (s[ItemDef::kAttack]) stats += std::format("  ATK {}", s[ItemDef::kAttack]);
        if (s[ItemDef::kDefense]) stats += std::format("  DEF {}", s[ItemDef::kDefense]);
    }
    if (def.durability > 0) stats += std::format("  DUR {}/{}", item.durability(), def.durability);
    if (def.weight > 0) stats += std::format("  WT {}", def.weight);
    if (def.requiredLevel > 1) stats += std::format("  LV {}", def.requiredLevel);
    if (def.effect[1] > 0) stats += std::format("  +{} HP", def.effect[1]);
    if (!stats.empty()) lines.emplace_back(stats.substr(2), unusable ? kRed : kWhite);
    // Its rolled modifiers, shortened.
    static constexpr const char* kModNames[ItemDef::kModifierCount] = {
        "ATK", "DEF", "HIT", "EVA", "MATK", "MDEF", "MHIT", "MEVA", "SPD", "WALK", "HP", "MP", "STR",
        "MLV", "STUN%", "STUN", nullptr, "REGEN", "MREGEN", "MCOST", "REFL%", "REFL", "TABLET", "CAPSULE",
        "ABS%", "ABS", "GOLD", "MINE", "MINES", "CHANT", "DOG HP", "DOG ATK", "DOG HIT", "DOG DEF",
        "DOG EVA", "DOG MATK", "DOG MHIT", "DOG MDEF", "DOG MEVA"};
    std::string mods;
    for (int i = 0; i < ItemDef::kModifierCount && !item.unidentified; ++i) {
        const int v = item.modifiers[static_cast<std::size_t>(i)];
        if (v == 0 || !kModNames[i]) continue;
        // Recovery in tenths of a percent a second, as the original's
        // tooltip shows it (0x40aed0).
        if (i == ItemDef::kModLifeRecovery || i == ItemDef::kModMentalRecovery) {
            mods += std::format("{}{}{}{}.{}%/s", mods.empty() ? "" : " ", kModNames[i], v < 0 ? "-" : "+", std::abs(v) / 10,
                                std::abs(v) % 10);
        } else {
            mods += std::format("{}{}{:+d}", mods.empty() ? "" : " ", kModNames[i], v);
        }
    }
    if (!mods.empty()) lines.emplace_back(mods, SDL_Color{150, 200, 255, 255});

    int w = 0;
    for (auto& [text, colour] : lines) {
        if (text.size() > 50) text.resize(fontCut(text, 50));
        w = std::max(w, uiTextWidth(text));
    }
    w += 8;
    const int h = static_cast<int>(lines.size()) * 13 + 5;
    const int x = std::clamp(mx - w / 2, 1, kScreenWidth - w);
    const int y = std::clamp(my + 8, 1, kScreenHeight - h);
    frame(renderer_, {x - 1, y - 1, w + 1, h + 1}, {0, 0, 0, 170}, {255, 255, 255, 128});
    for (std::size_t i = 0; i < lines.size(); ++i) {
        uiText(x + 4, y + 3 + static_cast<int>(i) * 13, lines[i].first, lines[i].second);
    }
}

void Game::renderInventory() {
    int mx = 0, my = 0;
    if (const auto at = pointerPosition()) {
        // The pointer, or where --pointer puts it.
        mx = static_cast<int>(at->x);
        my = static_cast<int>(at->y);
    } else {
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
        drawnPanel_ = picture;
        if (!picture) frame(renderer_, kPanel, {20, 18, 26, 225}, {150, 130, 90, 255});
        // Equipment slots.
        for (const auto& box : kSlots) {
            const SDL_Rect r = box.rect();
            if (!picture) frame(renderer_, r, {45, 40, 50, 255}, {110, 100, 80, 255});
            if (const auto& worn = inventory_.equipped(box.slot)) {
                drawItemPicture(*worn->def, r);
                drawItemMarks(*worn, r);
                if (inside(r, mx, my)) hover = &*worn;
            }
        }
        renderWeaponSetTabs();
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
            drawItemMarks(p.item, r);
            if (inside(r, mx, my)) hover = &p.item;
        }
        renderItemTotals();
        renderSpecialButton(mx, my);
        if (hover && !hand_) renderItemPopup(*hover, mx, my);
    }
    // The warehouse's item under the pointer gets the same box as the
    // bag's (0x409000, 0x4090b0 call 0x409160 as the bag does), drawn here
    // so the item window doesn't cover it.
    if (storageHover_ && !hand_ && storageOpen()) renderItemPopup(*storageHover_, mx, my);
    storageHover_ = nullptr;
    if (specialOpen_) renderSpecialWindow(mx, my);
    if (hand_) {
        const int w = hand_->def->width * kCell, h = hand_->def->height * kCell;
        drawItemPicture(*hand_->def, {mx - w / 2, my - h / 2, w, h});
        drawItemMarks(*hand_, {mx - w / 2, my - h / 2, w, h});
    }
}

// Which belt cell a screen point is over, or false.
namespace {
bool beltCellAt(int x, int y, int& cx, int& cy) {
    for (int row = 0; row < Inventory::kBeltHeight; ++row) {
        const int left = row == 0 ? kBeltLeft : kBeltBottomLeft, top = kBeltTop + row * kBeltCell;
        if (x >= left && x < left + Inventory::kBeltWidth * kBeltCell && y >= top && y < top + kBeltCell) {
            cx = (x - left) / kBeltCell;
            cy = row;
            return true;
        }
    }
    return false;
}
}  // namespace

// Keys 1-8 and a right click on a belt cell use what is there; a like
// item from the bag is used up first, so the belt stays stocked.
// Carries out one thing done with the hero's items (logic/item_request.h),
// checking it first: the item window's clicks come here, and online a
// player's requests to the server will too.
void Game::applyItemRequest(const ItemRequest& r) {
    using K = ItemRequest::Kind;
    if (!r.valid()) return;
    const auto slot = static_cast<EquipSlot>(r.x);
    switch (r.kind) {
        case K::BagClick:
            if (hand_ && hand_->def->specialPage >= 0) {
                // Put down in the bag, it goes back to its place in the special
                // items window, silently, unless one like it is there (0x446320).
                if (inventory_.addSpecial(*hand_)) hand_.reset();
            } else if (hand_) {
                const ItemDef& held = *hand_->def;
                const auto result = inventory_.clickBag(hand_, r.x, r.y);
                if (result == Inventory::Result::NoRoom) showMessage("No room there");
                // Put down in empty cells, or over one item, which is taken up (0x446320).
                if (result == Inventory::Result::Done) playSound(itemSound(held, hand_ ? kItemTake : kItemPut));
            } else if (inventory_.clickBag(hand_, r.x, r.y) == Inventory::Result::Done) {
                playSound(itemSound(*hand_->def, kItemTake));
            }
            break;
        case K::SlotClick: {
            const ItemDef* held = hand_ ? hand_->def : nullptr;
            switch (inventory_.clickSlot(hand_, slot, playerLevel_)) {
                case Inventory::Result::Done:
                    // Wearing an item, or taking one off (0x446320).
                    playSound(held ? itemSound(*held, kItemEquip) : itemSound(*hand_->def, kItemTake));
                    updateEquipment();
                    break;
                case Inventory::Result::WrongSlot: showMessage("That does not go there"); break;
                case Inventory::Result::LevelTooLow: showMessage(std::format("Needs level {}", hand_->def->requiredLevel)); break;
                default: break;
            }
            break;
        }
        case K::BeltClick: {
            // Taking an item, or putting one there, both with event 0 (0x445bd0).
            const ItemDef* held = hand_ ? hand_->def : nullptr;
            const auto result = inventory_.clickBelt(hand_, r.x, r.y);
            if (result == Inventory::Result::WrongSlot) showMessage("Only potions go in the belt");
            if (result == Inventory::Result::Done) playSound(itemSound(held ? *held : *hand_->def, kItemTake));
            break;
        }
        case K::UseBag: useItem(inventory_.indexAt(r.x, r.y)); break;
        case K::UseBelt: useBelt(r.x, r.y); break;
        case K::EquipFromBag: {
            const int index = inventory_.indexAt(r.x, r.y);
            if (!rules_.rightClickEquips || hand_ || index < 0) break;
            if (inventory_.slotFor(*inventory_.bag()[static_cast<std::size_t>(index)].item.def)) equipFromBag(index);
            break;
        }
        case K::UnequipToBag:
            if (!hand_ && rules_.rightClickEquips) unequipToBag(slot);
            break;
        case K::DropHand: dropHand(); break;
        case K::ChooseWeaponSet:
            if (inventory_.chooseWeaponSet(r.x)) {
                playSound(0x3a);
                updateEquipment();
            }
            break;
        case K::ToggleShared:
            // Only the tab in use answers: sharing its empty slot with the
            // other set, or ending that. Silent.
            if (inventory_.toggleShared(r.x, r.y)) updateEquipment();
            break;
        case K::SplitGold:
            if (inventory_.splitGold(inventory_.indexAt(r.x, r.y), r.amount, hand_)) playSound(itemSound(*hand_->def, kItemTake));
            break;
        case K::ToWarehouse: putInWarehouse(); break;
        case K::MineSlot: mineSlotClick(); break;
        case K::Buy: buyAt(r.x, r.y); break;
        case K::SellHand: sellHand(); break;
        case K::SellFromBag:
            if (rules_.rightClickSells) sellFromBag(inventory_.indexAt(r.x, r.y));
            break;
        case K::StorageClick: {
            if (!storageOpen()) break;
            ShopStock& stock = openStock();
            if (hand_) {
                const ItemDef& held = *hand_->def;
                const auto result = stock.click(hand_, r.x, r.y);
                if (result == Inventory::Result::NoRoom) showMessage("No room there");
                // Put down in empty cells, or over one item, which is taken up (0x447970).
                if (result == Inventory::Result::Done) playSound(itemSound(held, hand_ ? kItemTake : kItemPut));
            } else if (stock.click(hand_, r.x, r.y) == Inventory::Result::Done) {
                playSound(itemSound(*hand_->def, kItemTake));
            }
            break;
        }
        case K::StoreFromBag:
            if (rules_.rightClickStores) storeFromBag(inventory_.indexAt(r.x, r.y));
            break;
        case K::FromStorage: {
            if (!storageOpen() || hand_) break;
            ShopStock& stock = openStock();
            const int i = stock.indexAt(r.x, r.y);
            if (i < 0 || stock.goods[static_cast<std::size_t>(i)].item.def->isGold()) break;
            if (rules_.rightClickStores) {
                takeFromStorage(i);
            } else if (applyConsumable(*stock.goods[static_cast<std::size_t>(i)].item.def)) {
                stock.goods.erase(stock.goods.begin() + i);
            }
            break;
        }
        case K::SplitStorageGold: {
            if (!storageOpen()) break;
            ShopStock& stock = openStock();
            if (splitGold(stock.goods, stock.indexAt(r.x, r.y), r.amount, items_.find(ItemDef::Class::Special, 0), hand_)) {
                playSound(itemSound(*hand_->def, kItemTake));
            }
            break;
        }
        case K::GiantPage:
            if (openShop_ == kGiantStorage && giantPageOpen(r.x) && r.x != giantPage_) {
                giantPage_ = r.x;
                playSound(0x3a);
            }
            break;
        case K::CloseShop: closeShop(); break;
        case K::Identify: {
            // Only an unknown item in the bag can be chosen, with an empty
            // hand; that ends the spell and trains it (0x446320).
            if (!identifying_ || hand_) break;
            const int index = inventory_.indexAt(r.x, r.y);
            if (!inventory_.identify(index)) break;
            identifying_ = false;
            trainSpell(kIdentify);
            showMessage(inventory_.bag()[static_cast<std::size_t>(index)].item.shownName());
            break;
        }
        case K::CancelIdentify:
            if (!identifying_) break;
            identifying_ = false;  // the right button calls it off (0x445a40)
            showMessage("Identify cancelled");
            break;
    }
}

void Game::useBelt(int x, int y) {
    const auto& item = inventory_.beltAt(x, y);
    if (!item || hand_) return;
    if (applyConsumable(*item->def)) inventory_.useUpBelt(x, y);
}

bool Game::beltClick(int x, int y, bool right) {
    int cx, cy;
    if (menuOpen_ || !player_ || !beltCellAt(x, y, cx, cy)) return false;
    applyItemRequest(ItemRequest::at(right ? ItemRequest::Kind::UseBelt : ItemRequest::Kind::BeltClick, cx, cy));
    return true;
}

void Game::renderBelt() {
    const bool panel = hasPanelArt();
    for (int row = 0; row < Inventory::kBeltHeight; ++row) {
        for (int col = 0; col < Inventory::kBeltWidth; ++col) {
            const SDL_Rect r{(row == 0 ? kBeltLeft : kBeltBottomLeft) + col * kBeltCell, kBeltTop + row * kBeltCell, kBeltCell,
                             kBeltCell};
            // The panel has the pockets and their keys drawn in.
            if (!panel) frame(renderer_, r, {20, 18, 26, 170}, {110, 100, 80, 220});
            if (const auto& item = inventory_.beltAt(col, row)) drawItemPicture(*item->def, r);
            if (panel) continue;
            // Its key, small in the bottom right corner, as on the original's panel.
            drawText(renderer_, r.x + r.w - 6, r.y + r.h - 8, std::to_string(row * Inventory::kBeltWidth + col + 1),
                     {200, 190, 160, 255}, 1);
        }
    }
}

void Game::renderHud() {
    if (menuOpen_) return;
    if (player_) renderBelt();
    if (player_) renderMineButton();
    // The original shows the gold only in the item window.
    if (player_ && !showInventory_ && !hasPanelArt()) {
        drawText(renderer_, 8, kScreenHeight - 46, std::format("GOLD {}", inventory_.gold()), kGoldColor);
    }
    if (messageTicks_ > 0 && !message_.empty()) {
        // Names in Japanese need the original's font; the stand-in one has
        // no such letters.
        if (std::ranges::any_of(message_, [](char c) { return fontLeadByte(static_cast<unsigned char>(c)); }) && uiFont()) {
            const int x = (kScreenWidth - uiTextWidth(message_)) / 2;
            uiText(x + 1, 37, message_, {0, 0, 0, 255});
            uiText(x, 36, message_, kWhite);
        } else {
            drawText(renderer_, (kScreenWidth - textWidth(message_)) / 2, 40, message_, kWhite);
        }
    }
}

}  // namespace sf
