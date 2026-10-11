#pragma once

#include <array>
#include <optional>
#include <vector>

#include "formats/item_data.h"

namespace sf {

class Random;

// One item the player has or that lies somewhere.
struct Item {
    const ItemDef* def = nullptr;
    int count = 1;  // gold: the amount
};

// Where worn items go, as in ShadowFlare.exe's item window (slot table
// built at 0x407170).
enum class EquipSlot { Helmet, Body, Boots, Weapon, Shield, Accessory1, Accessory2, Accessory3, Accessory4 };
inline constexpr int kEquipSlotCount = 9;

// The player's bag, worn equipment and gold. Pure rules, no drawing: the
// game turns mouse clicks into cell and slot numbers.
class Inventory {
public:
    // The bag is 9 x 4 cells of 32 pixels (player +0x514).
    static constexpr int kBagWidth = 9, kBagHeight = 4;

    struct Placed {
        Item item;
        int x = 0, y = 0;  // top-left cell
    };

    enum class Result {
        Done,
        Nothing,      // empty cell or slot and an empty hand
        NoRoom,       // the item does not fit there
        WrongSlot,    // that slot does not take this kind of item
        LevelTooLow,  // the player's level is below the item's required level
    };

    const std::vector<Placed>& bag() const { return bag_; }
    const std::optional<Item>& equipped(EquipSlot slot) const { return equipped_[static_cast<int>(slot)]; }
    int gold() const { return gold_; }
    void addGold(int amount) { gold_ += amount; }
    void clear();

    // Whether a w x h item fits with its top-left at (x, y).
    bool fits(int w, int h, int x, int y) const;
    // The bag entry covering a cell, or -1.
    int indexAt(int x, int y) const;
    // Puts an item in the first place it fits, column by column from the
    // left. Gold goes to the gold counter. False when the bag is full.
    bool add(const Item& item);
    bool addAt(const Item& item, int x, int y);

    // Left click on a bag cell. With an empty hand, picks up the item
    // there. With an item in hand, puts it down with its top-left corner
    // at (x, y); if it covers exactly one item, that one is picked up
    // instead (they swap).
    Result clickBag(std::optional<Item>& hand, int x, int y);

    // Left click on an equipment slot: takes off what is worn there, puts
    // on what is in hand, or swaps the two.
    Result clickSlot(std::optional<Item>& hand, EquipSlot slot, int level);

    static bool accepts(EquipSlot slot, const ItemDef& def);
    // The slot an item goes in when it is equipped without choosing one:
    // the first free accessory slot for accessories.
    std::optional<EquipSlot> slotFor(const ItemDef& def) const;
    Result canEquip(EquipSlot slot, const ItemDef& def, int level) const;
    // Wears an item without checking it; returns what was there.
    std::optional<Item> equip(EquipSlot slot, const Item& item);

    // Uses one consumable from the bag (right click), removing it. Returns
    // what it was, or null when the entry is not a consumable.
    const ItemDef* use(int index);

    // The ten stats of everything worn, added up.
    std::array<int, ItemDef::kStatCount> stats() const;
    // Layers of the hero's animation to draw: 0 (body) and 1 (shadow), then
    // the body armour's, the weapon's and its glow, and the shield's unless
    // the weapon is two-handed. Helmets, boots and accessories are not
    // drawn (ShadowFlare.exe 0x444ca0).
    std::vector<int> heroLayers() const;

private:
    std::vector<Placed> bag_;
    std::array<std::optional<Item>, kEquipSlotCount> equipped_;
    int gold_ = 0;
};

// Damage after the defender's defense: each point of defense takes off
// about 1% (a stand-in; the original's formula is not known yet). At least 1.
int reduceDamage(int damage, int defense);

// What a dying monster leaves. Gold, as ShadowFlare.exe does it
// (0x45a030): `chance` percent of the time, an amount from min to max.
// Returns 0 for none.
int rollGold(int chance, int min, int max, Random& random);

// An item for a monster of `level` to drop, or null: a stand-in for the
// game's drop tables (Table.Tbd tables 30 and 31, not decoded yet). Picks
// among normal-quality weapons, armour, accessories and consumables whose
// item level is at most the monster's, weighted by their drop weight.
const ItemDef* pickDrop(const ItemTable& items, int level, Random& random);

}  // namespace sf
