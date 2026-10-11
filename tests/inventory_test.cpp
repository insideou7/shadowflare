// Tests for the inventory and equipment rules (src/logic/inventory.h).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

#include "logic/ai_select.h"
#include "logic/inventory.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

using Class = sf::ItemDef::Class;
using Result = sf::Inventory::Result;
using Slot = sf::EquipSlot;

sf::ItemDef def(Class cls, int subtype, int w, int h, int layer = -1) {
    sf::ItemDef d;
    d.cls = cls;
    d.subtype = subtype;
    d.width = w;
    d.height = h;
    d.heroLayer = layer;
    d.requiredLevel = 1;
    d.itemLevel = 1;
    return d;
}

const sf::ItemDef kSword = [] {
    auto d = def(Class::Weapon, sf::ItemDef::kSword, 1, 4, 12);
    d.stats[sf::ItemDef::kAttack] = 20;
    return d;
}();
const sf::ItemDef kGreatSword = def(Class::Weapon, sf::ItemDef::kGreatSword, 2, 4, 19);
const sf::ItemDef kCloth = [] {
    auto d = def(Class::Armour, sf::ItemDef::kBody, 2, 3, 5);
    d.stats[sf::ItemDef::kDefense] = 20;
    return d;
}();
const sf::ItemDef kShield = [] {
    auto d = def(Class::Armour, sf::ItemDef::kShield, 2, 2, 9);
    d.stats[sf::ItemDef::kDefense] = 8;
    return d;
}();
const sf::ItemDef kHelmet = def(Class::Armour, sf::ItemDef::kHelmet, 2, 2, 5);
const sf::ItemDef kRing = def(Class::Accessory, 0, 1, 1);
const sf::ItemDef kTablet = [] {
    auto d = def(Class::Consumable, -1, 1, 1);
    d.effect[1] = 200;
    return d;
}();
const sf::ItemDef kGold = def(Class::Special, -1, 1, 1);
const sf::ItemDef kHeavy = [] {
    auto d = def(Class::Weapon, sf::ItemDef::kSword, 1, 4, 17);
    d.requiredLevel = 30;
    return d;
}();

void testBagPlacement() {
    sf::Inventory inv;
    // The starting kit: four tablets fill the first column, top to bottom.
    for (int i = 0; i < 4; ++i) check(inv.add({&kTablet}), "tablet fits");
    check(inv.indexAt(0, 0) >= 0 && inv.indexAt(0, 3) >= 0 && inv.indexAt(1, 0) < 0, "first column filled");
    check(inv.add({&kSword}), "sword fits");
    check(inv.bag().back().x == 1 && inv.bag().back().y == 0, "sword goes in the next free column");
    check(inv.indexAt(1, 3) == inv.indexAt(1, 0), "a tall item covers all its cells");
    check(!inv.fits(2, 2, 8, 0), "items do not stick out of the bag");
    check(!inv.fits(1, 1, 1, 2), "items do not overlap");
    check(inv.fits(2, 2, 7, 2), "free corner");
    check(inv.add({&kGold, 150}) && inv.gold() == 150 && inv.bag().size() == 5, "gold goes to the counter");

    // Fill the bag with 2x4 items: 4 more fit in columns 2-9.
    for (int i = 0; i < 3; ++i) check(inv.add({&kGreatSword}), "great sword fits");
    check(inv.bag().size() == 8, "bag holds 8 items");
    check(!inv.add({&kGreatSword}), "a full bag refuses more");
    check(inv.add({&kTablet}), "a small item still fits the last column");
}

void testBagClicks() {
    sf::Inventory inv;
    std::optional<sf::Item> hand;
    check(inv.clickBag(hand, 0, 0) == Result::Nothing && !hand, "click on an empty cell");
    inv.addAt({&kSword}, 0, 0);
    inv.addAt({&kTablet}, 3, 0);
    inv.addAt({&kTablet}, 3, 1);
    check(inv.clickBag(hand, 0, 2) == Result::Done && hand && hand->def == &kSword && inv.bag().size() == 2,
          "pick up by any cell of the item");
    check(inv.clickBag(hand, 8, 1) == Result::NoRoom && hand, "a 1x4 item cannot go below the top row at the edge");
    check(inv.clickBag(hand, 3, 0) == Result::NoRoom && hand, "covering two items is refused");
    check(inv.clickBag(hand, 5, 0) == Result::Done && !hand && inv.indexAt(5, 3) >= 0, "put down in free space");
    // Putting the shield over a single tablet swaps them.
    hand = sf::Item{&kShield};
    check(inv.clickBag(hand, 2, 0) == Result::NoRoom && hand->def == &kShield, "shield over both tablets");
    sf::Inventory one;
    one.addAt({&kTablet}, 0, 0);
    check(one.clickBag(hand, 0, 0) == Result::Done && hand && hand->def == &kTablet, "swap with the covered item");
    check(one.bag().size() == 1 && one.bag()[0].item.def == &kShield, "the shield is now in the bag");
}

void testEquip() {
    sf::Inventory inv;
    std::optional<sf::Item> hand = sf::Item{&kSword};
    check(inv.clickSlot(hand, Slot::Shield, 1) == Result::WrongSlot && hand, "a sword is not a shield");
    check(inv.clickSlot(hand, Slot::Weapon, 1) == Result::Done && !hand, "equip a sword");
    hand = sf::Item{&kHeavy};
    check(inv.clickSlot(hand, Slot::Weapon, 10) == Result::LevelTooLow && hand->def == &kHeavy, "required level");
    check(inv.clickSlot(hand, Slot::Weapon, 30) == Result::Done && hand && hand->def == &kSword, "swap weapons");
    hand.reset();
    check(inv.clickSlot(hand, Slot::Weapon, 30) == Result::Done && hand->def == &kHeavy && !inv.equipped(Slot::Weapon),
          "take off");
    check(inv.clickSlot(hand, Slot::Body, 1) == Result::WrongSlot, "weapon on the body");
    hand = sf::Item{&kHelmet};
    check(inv.clickSlot(hand, Slot::Body, 1) == Result::WrongSlot && inv.clickSlot(hand, Slot::Helmet, 1) == Result::Done,
          "helmets go on the head");
    hand = sf::Item{&kRing};
    check(inv.clickSlot(hand, Slot::Accessory3, 1) == Result::Done, "rings in any accessory slot");
    check(inv.slotFor(kRing) == Slot::Accessory1, "first free accessory slot");
    check(inv.slotFor(kTablet) == std::nullopt, "consumables are not worn");
    hand = sf::Item{&kTablet};
    check(inv.clickSlot(hand, Slot::Accessory1, 1) == Result::WrongSlot, "tablet is not an accessory");
}

void testStatsAndLayers() {
    sf::Inventory inv;
    check(inv.heroLayers() == std::vector<int>{0, 1}, "body and shadow only");
    inv.equip(Slot::Body, {&kCloth});
    inv.equip(Slot::Weapon, {&kSword});
    inv.equip(Slot::Shield, {&kShield});
    inv.equip(Slot::Helmet, {&kHelmet});
    const auto stats = inv.stats();
    check(stats[sf::ItemDef::kAttack] == 20 && stats[sf::ItemDef::kDefense] == 28, "worn stats add up");
    check(inv.heroLayers() == std::vector<int>({0, 1, 5, 12, 9}), "armour, weapon and shield layers; no helmet");
    inv.equip(Slot::Weapon, {&kGreatSword});
    check(inv.heroLayers() == std::vector<int>({0, 1, 5, 19}), "a two-handed weapon hides the shield");
}

void testUse() {
    sf::Inventory inv;
    inv.add({&kSword});
    inv.add({&kTablet});
    check(inv.use(0) == nullptr && inv.bag().size() == 2, "a sword cannot be used");
    check(inv.use(1) == &kTablet && inv.bag().size() == 1, "a tablet is used up");
    check(inv.use(5) == nullptr, "out of range");
}

void testLoot() {
    sf::Random random(7);
    int drops = 0;
    for (int i = 0; i < 1000; ++i) {
        const int gold = sf::rollGold(10, 5, 8, random);
        check(gold == 0 || (gold >= 5 && gold <= 8), "gold amount in range");
        drops += gold > 0;
    }
    check(drops > 50 && drops < 150, "gold drops about 10% of the time");
    check(sf::rollGold(0, 5, 8, random) == 0, "no chance, no gold");

    sf::ItemTable table;
    table.items = {kSword, kHeavy, kGold, kTablet};
    table.items[1].itemLevel = 30;
    table.items[2].itemLevel = 0;
    bool sawSword = false, sawTablet = false;
    for (int i = 0; i < 200; ++i) {
        const sf::ItemDef* d = sf::pickDrop(table, 1, random);
        check(d && d->cls != Class::Special && d->itemLevel <= 1, "drops fit the monster's level and are never gold");
        sawSword |= d && d->cls == Class::Weapon;
        sawTablet |= d && d->cls == Class::Consumable;
    }
    check(sawSword && sawTablet, "every allowed item can drop");
    check(sf::reduceDamage(10, 0) == 10 && sf::reduceDamage(10, 100) == 5 && sf::reduceDamage(1, 500) == 1,
          "defense reduces damage but never to nothing");
    sf::ItemTable none;
    check(sf::pickDrop(none, 5, random) == nullptr, "nothing to drop");
}

}  // namespace

int main() {
    testBagPlacement();
    testBagClicks();
    testEquip();
    testStatsAndLayers();
    testUse();
    testLoot();
    if (failures == 0) std::puts("all inventory tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
