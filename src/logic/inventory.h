#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "formats/item_data.h"

namespace sf {

class Random;
struct GameTables;

// What each point of a magic modifier adds to an item's price: Table.Tbd
// table 34, rows 0-7 the element modifiers, then one row per modifier
// (0x4661c0). All 0 without the table (open content).
struct ModifierPrices {
    std::array<int, 8> element{};
    std::array<int, ItemDef::kModifierCount> modifier{};
    static ModifierPrices load(const GameTables& tables);
};

// One item the player has or that lies somewhere.
struct Item {
    const ItemDef* def = nullptr;
    int count = 1;  // gold: the amount
    // Weapons and armour: how worn it is. It breaks, and stops counting
    // towards the hero's stats, when this reaches the item's durability.
    // The exe keeps the points left instead (+0xbc, out of +0xb8).
    int wear = 0;
    bool unidentified = false;  // the exe's flag at +0x1c, inverted
    // Its rolled magic modifiers (ItemDef::Modifier) and element modifiers.
    std::array<std::int32_t, ItemDef::kModifierCount> modifiers{};
    std::array<std::int32_t, 8> elementModifiers{};

    // A newly made item: magic and unique ones start unidentified, as
    // ShadowFlare.exe makes them (0x462d30). The second form also rolls
    // its modifiers, as every new item gets.
    static Item make(const ItemDef* def, int count = 1);
    static Item make(const ItemDef* def, Random& random, int count = 1);
    // Each modifier: `chance` percent of the time a number from its range,
    // otherwise 0 (0x467310 for weapons, 0x466570 armour, 0x465730
    // accessories).
    void rollModifiers(Random& random);

    // Durability points left, and whether it is broken (0 left). Items
    // with no durability never wear out.
    int durability() const;
    bool broken() const { return def->durability > 0 && wear >= def->durability; }
    // Its name, or the plain one before it is identified.
    const std::string& shownName() const;
    // What it is worth (0x4661c0 with 0): its price plus what its
    // modifiers add, unless the price is fixed. Shops sell at this.
    int value(const ModifierPrices& prices) const;
    // What a shop pays (0x4661c0 with 1): a quarter of its value, of the
    // durability left for weapons and armour, at least 1.
    int sellValue(const ModifierPrices& prices) const;
    // What mending it costs (0x4667a0): nothing when it is not worn,
    // otherwise the worn share of a tenth of its value, at least 1. Magic
    // items cost more to mend because their modifiers raise the value.
    int repairPrice(const ModifierPrices& prices) const;
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

    // Wear from fighting, as ShadowFlare.exe does it: each blow the hero
    // aims wears the weapon one point 30% of the time (0x4362cb and the
    // other weapon attacks); each blow the hero takes wears the helmet and
    // boots 20% and the body armour and shield 30% of the time, but not a
    // shield hidden by a two-handed weapon (0x443fa8). Broken items stop
    // wearing. Return the slots of what broke.
    std::vector<EquipSlot> wearFromAttacking(Random& random);
    std::vector<EquipSlot> wearFromBlow(Random& random);

    bool hasUnidentified() const;
    bool carries(ItemDef::Class cls, std::int32_t code) const;  // in the bag  // CHECKUNOPINIONITEM (0x43397d)
    void identifyAll();            // IDENTIFY: everything worn and in the bag (0x432296)

    // What REPAIR and GETREPAIRPRICE call a part (0x43234a, 0x4310d7):
    // 0 weapon, 1 helmet, 2 body, 3 shield, 4 boots; -1 every weapon and
    // armour in the bag. GETREPAIRPRICE also prices the bag for any other
    // number, REPAIR does nothing for one.
    static constexpr int kRepairBag = -1;
    int repairPrice(int part, const ModifierPrices& prices) const;
    void repair(int part);

    // The ten stats of everything worn and not broken, added up, with
    // their modifiers (0x44eb5b).
    std::array<int, ItemDef::kStatCount> stats() const;
    // One modifier of everything worn, added up, as 0x44eb5b and 0x4138b0
    // count them: broken weapons and armour don't count, nor a shield
    // hidden by a two-handed weapon.
    int modifier(ItemDef::Modifier which) const;
    // Their element levels with their element modifiers, added up
    // (0x44fe30). Broken items still count: that code, unlike the stats
    // at 0x44eb5b, does not check; a shield hidden by a two-handed weapon
    // does not.
    std::array<int, 8> elements() const;
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
