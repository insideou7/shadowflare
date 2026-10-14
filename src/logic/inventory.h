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

// The item sounds of 0x466110, as voices of Voice00.Voc, -1 for none.
// Events: 0 taking an item into the hand (and putting it on the belt, in a
// shop or over another item), 1 putting it down in a grid, 2 not used by
// the original, 3 wearing it, 4 the hero's swing with it as a weapon (1
// light, 2 heavy).
enum ItemSoundEvent { kItemTake = 0, kItemPut = 1, kItemUnused = 2, kItemEquip = 3, kItemSwing = 4 };
int itemSound(const ItemDef& def, int event);

// Where worn items go, as in ShadowFlare.exe's item window (slot table
// built at 0x407170).
enum class EquipSlot { Helmet, Body, Boots, Weapon, Shield, Accessory1, Accessory2, Accessory3, Accessory4 };
inline constexpr int kEquipSlotCount = 9;

// The gold split box (ShadowFlare.exe 0x4429b0): takes `amount` gold from
// the Gold stack at `index` of a bag or warehouse's list into an empty
// hand. Less than the stack makes a new Gold item of that much; all of it
// takes the stack itself. False, with nothing changed, for any other
// amount, a stack that isn't gold or a full hand.
template <class Placed>
bool splitGold(std::vector<Placed>& list, int index, int amount, const ItemDef* goldDef, std::optional<Item>& hand) {
    if (hand || index < 0 || static_cast<std::size_t>(index) >= list.size() || amount <= 0) return false;
    Item& stack = list[static_cast<std::size_t>(index)].item;
    if (!stack.def->isGold() || amount > stack.count) return false;
    if (amount == stack.count) {
        hand = stack;
        list.erase(list.begin() + index);
        return true;
    }
    hand = Item::make(goldDef ? goldDef : stack.def, amount);
    stack.count -= amount;
    return true;
}

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
        Locked,       // the weapon or shield slot is shared with the other weapon set
    };

    // The belt pocket, 4 x 2 cells under the HP and MP gauges (player
    // +0x518): consumables only. Keys 1-4 use the top row, 5-8 the bottom.
    static constexpr int kBeltWidth = 4, kBeltHeight = 2;

    const std::vector<Placed>& bag() const { return bag_; }
    const std::optional<Item>& equipped(EquipSlot slot) const { return equipped_[static_cast<int>(slot)]; }
    // Gold is carried in the bag as Gold items (special 0) of up to 10000
    // each, as in the original; the item window adds them up (0x466d70).
    // Without a Gold item to make stacks of, a plain counter stands in.
    static constexpr int kGoldStack = 10000;
    void setGoldDef(const ItemDef* def) { goldDef_ = def; }
    int gold() const;
    int looseGold() const { return gold_; }  // the counter's part
    // Positive amounts go into the bag (0x466e10): the stacks there are
    // topped up to 10000 in bag order, then new stacks fill free cells row
    // by row. Negative ones are paid from the smallest stack first
    // (0x466da0). False, with nothing changed, when there is not room or
    // not enough.
    bool addGold(int amount);
    // The gold split box on a bag stack (see splitGold).
    bool splitGold(int index, int amount, std::optional<Item>& hand) { return sf::splitGold(bag_, index, amount, goldDef_, hand); }
    void clear();

    // Whether a w x h item fits with its top-left at (x, y).
    bool fits(int w, int h, int x, int y) const;
    // The bag entry covering a cell, or -1.
    int indexAt(int x, int y) const;
    // Puts an item in the first place it fits, column by column from the
    // left. Gold joins the bag's gold (see addGold). False when the bag is
    // full.
    bool add(const Item& item);
    bool addAt(const Item& item, int x, int y);
    // An item picked up from the ground (0x449ef0): consumables marked for
    // the belt (effect word 0) go in its first free cell, row by row, and
    // otherwise, like everything else, in the bag.
    bool pickUp(const Item& item);

    // The belt. Only 1 x 1 consumables go in it.
    const std::optional<Item>& beltAt(int x, int y) const;
    static bool fitsBelt(const ItemDef& def);
    bool addToBelt(const Item& item);
    bool addToBeltAt(const Item& item, int x, int y);
    // Left click on a belt cell, from the HUD (0x445bd0): picks up what is
    // there, or puts a consumable from the hand there, swapping.
    Result clickBelt(std::optional<Item>& hand, int x, int y);
    // After the item in a belt cell was used: one like it from the bag is
    // used up if there is one, so the belt stays stocked, otherwise the
    // one in the belt (0x44a5f0, and the keys at 0x442ed6).
    void useUpBelt(int x, int y);

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
    // A right click on a bag item (ours, Rules::rightClickEquips): wears it
    // in the slot slotFor picks. What was worn there goes in the bag, where
    // the new item was if it fits there, otherwise in the first place it
    // fits; with no room for it nothing changes (NoRoom).
    Result equipFromBag(int index, int level);
    // A right click on a worn item: takes it off into the bag's first free
    // place, or NoRoom.
    Result unequipToBag(EquipSlot slot);

    // The second weapon set (Caps Lock, and the I and II tabs under the
    // weapon and shield slots; ShadowFlare.exe 0x406aa0 and 0x451930). The
    // set not in use keeps its weapon and shield aside (player +0x50c and
    // +0x510). Each set's weapon and shield can instead be shared with the
    // other set: a right click on the tab in use while its slot is empty
    // brings the other set's item over and keeps it there whichever set is
    // in use, and the slot can't be changed until another right click
    // puts it back with the other set.
    enum WeaponSetPart { kSetWeapon = 0, kSetShield = 1 };
    int weaponSet() const { return weaponSet_; }  // 0 is set I, 1 set II
    const std::optional<Item>& spare(int part) const { return spare_[static_cast<std::size_t>(part)]; }
    bool shared(int part, int set) const { return shared_[static_cast<std::size_t>(part * 2 + set)]; }
    // Whether the weapon or shield slot in use is shared, and so can't be
    // changed.
    bool slotShared(EquipSlot slot) const;
    // Changes to the other set (0x451930): the weapon and the shield trade
    // places with the ones kept aside, unless they are shared.
    void swapWeaponSets();
    // A left click on tab `set`: changes to that set if it isn't the one in
    // use. Returns whether it changed.
    bool chooseWeaponSet(int set);
    // A right click on tab `set` of the weapon or shield row: only the tab
    // in use answers, sharing an empty slot or ending the sharing. Returns
    // whether an item moved.
    bool toggleShared(int part, int set);
    // A saved game's sets, without checking them.
    void setWeaponSets(int set, const std::array<bool, 4>& shared, const std::array<std::optional<Item>, 2>& spare);

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
    // A bowgun's shots are its durability: each pull of the trigger uses
    // one point, however many shots fly (0x437fe0, 0x438a90 by way of
    // 0x467550), and one with none left shoots nothing. Items with no
    // durability (the open content's, left out) never run out.
    bool canShoot() const;
    std::vector<EquipSlot> wearFromShot();

    bool hasUnidentified() const;
    bool carries(ItemDef::Class cls, std::int32_t code) const;  // in the bag  // CHECKUNOPINIONITEM (0x43397d)
    // Takes every such item out of the bag; returns how many there were.
    int takeAll(ItemDef::Class cls, std::int32_t code);
    // CHECKITEMEXIST and DELETEITEM: such an item in the bag or worn, and
    // taking the first one away, from the bag before what is worn.
    bool has(ItemDef::Class cls, std::int32_t code) const;
    bool removeOne(ItemDef::Class cls, std::int32_t code);
    void identifyAll();            // IDENTIFY: everything worn and in the bag (0x432296)
    // The Identify spell on one bag entry: false when it was already known.
    bool identify(int index);

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
    // One modifier of the armour and accessories worn, as a blow on the
    // hero counts them (0x450d90): broken items count, the weapon and a
    // shield hidden by a two-handed weapon do not.
    int guardModifier(ItemDef::Modifier which) const;
    // One modifier of everything worn, broken items included; a shield
    // hidden by a two-handed weapon does not count. The gold drop adds
    // Amount of Gold this way (0x44f8b0), the MP cost Mental Cost
    // (0x44f610).
    int modifierEvenBroken(ItemDef::Modifier which) const;
    int goldBonus() const { return modifierEvenBroken(ItemDef::kModGold); }
    // Their element levels with their element modifiers, added up
    // (0x44fe30). Broken items still count: that code, unlike the stats
    // at 0x44eb5b, does not check; a shield hidden by a two-handed weapon
    // does not.
    std::array<int, 8> elements() const;
    // How heavy everything worn is, as the item window adds it up
    // (0x445630): broken items count, a shield hidden by a two-handed
    // weapon does not.
    int wornWeight() const;
    // Layers of the hero's animation to draw: 0 (body) and 1 (shadow), then
    // the body armour's, the weapon's and its glow, and the shield's unless
    // the weapon is two-handed. Helmets, boots and accessories are not
    // drawn (ShadowFlare.exe 0x444ca0).
    std::vector<int> heroLayers() const;

private:
    std::vector<Placed> bag_;
    std::array<std::optional<Item>, kBeltWidth * kBeltHeight> belt_;
    std::array<std::optional<Item>, kEquipSlotCount> equipped_;
    int weaponSet_ = 0;
    std::array<std::optional<Item>, 2> spare_;  // the other set's weapon and shield
    std::array<bool, 4> shared_{};              // set I weapon, set II weapon, set I shield, set II shield
    int gold_ = 0;  // only without a Gold item
    const ItemDef* goldDef_ = nullptr;
};

}  // namespace sf
