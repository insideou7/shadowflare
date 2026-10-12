// Tests for the inventory and equipment rules (src/logic/inventory.h).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

#include "logic/ai_select.h"
#include "formats/binary_reader.h"
#include "logic/inventory.h"
#include "logic/save_game.h"

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
    sf::ItemTable none;
    check(sf::pickDrop(none, 5, random) == nullptr, "nothing to drop");
}

void testSaveGame() {
    sf::ItemTable table;
    auto sword = def(Class::Weapon, sf::ItemDef::kSword, 1, 4, 12);
    sword.code = 7;
    auto potion = def(Class::Consumable, 0, 1, 1);
    potion.code = 3;
    table.items = {sword, potion};

    sf::Inventory inventory;
    inventory.addGold(120);
    check(inventory.addAt({&table.items[1], 1}, 2, 1), "potion goes in the bag");
    inventory.equip(Slot::Weapon, {&table.items[0], 1});

    sf::SaveGame save;
    save.map = "waterworks";
    save.scenario = "Scenario/00000002";
    save.x = 1280;
    save.y = 1472;
    save.direction = 5;
    save.hp = 92;
    save.level = 3;
    save.experience = 140;
    save.flags[{sf::ScenarioScript::kSave, 71}] = 1;
    save.career = sf::Career::at(3);
    save.career->offered = sf::HeroClass::Wizard;
    save.career->countKill(8, false);
    save.store(inventory);
    save.mp = 17;
    save.spells.emplace();
    save.spells->level[sf::kFireBall] = 3;
    save.spells->uses[sf::kFireBall] = 2;
    save.spells->level[sf::kEnergyShield] = 1;
    save.spells->bar[2] = sf::kFireBall;
    save.spells->selected = sf::kFireBall;
    save.spellsOn[sf::kEnergyShield] = true;
    save.originalMode = true;

    const auto loaded = sf::SaveGame::parse(save.write());
    check(loaded.map == "waterworks" && loaded.scenario == "Scenario/00000002", "map and scenario survive");
    check(loaded.x == 1280 && loaded.y == 1472 && loaded.direction == 5, "position survives");
    check(loaded.hp == 92 && loaded.level == 3 && loaded.experience == 140 && loaded.gold == 120, "stats survive");
    check(loaded.flags.at({sf::ScenarioScript::kSave, 71}) == 1, "story flags survive");
    check(loaded.career && loaded.career->history == save.career->history && loaded.career->offered == sf::HeroClass::Wizard &&
              loaded.career->kills == 1 && loaded.career->killsBy[sf::Career::kMagic] == 1,
          "the class, its history and the kills survive");
    check(!sf::SaveGame::parse("version=1\nmap=a\n").career, "older saves have no class");
    check(loaded.mp == 17 && loaded.spells && loaded.spells->level[sf::kFireBall] == 3 && loaded.spells->uses[sf::kFireBall] == 2,
          "MP and spell levels survive");
    check(loaded.spells->bar[2] == sf::kFireBall && loaded.spells->bar[0] == -1 && loaded.spells->selected == sf::kFireBall,
          "the spell bar and the chosen spell survive");
    check(loaded.spellsOn[sf::kEnergyShield] && !loaded.spellsOn[sf::kFireBall] && loaded.originalMode,
          "shields that are on and original mode survive");
    const auto older = sf::SaveGame::parse("version=1\nmap=a\n");
    check(!older.spells && !older.mp && !older.originalMode, "older saves have no magic");

    sf::ShopStock warehouse;
    warehouse.goods.push_back({{&table.items[0], 1}, 4, 6});
    save.storeStorage(warehouse);
    const auto withStorage = sf::SaveGame::parse(save.write());
    sf::ShopStock reloaded;
    check(withStorage.restoreStorage(reloaded, table) == 0 && reloaded.goods.size() == 1 && reloaded.goods[0].x == 4 &&
              reloaded.goods[0].y == 6 && reloaded.goods[0].item.def == &table.items[0],
          "the warehouse survives");

    sf::Inventory restored;
    restored.addGold(5);
    check(loaded.restore(restored, table) == 0, "every item is known");
    check(restored.gold() == 120, "gold is replaced, not added");
    check(restored.bag().size() == 1 && restored.bag()[0].x == 2 && restored.bag()[0].y == 1, "bag items keep their place");
    const auto& worn = restored.equipped(Slot::Weapon);
    check(worn && worn->def == &table.items[0], "worn items are worn again");

    table.items.pop_back();
    sf::Inventory partial;
    check(loaded.restore(partial, table) == 1, "unknown items are skipped");

    auto threw = [](std::string_view text) {
        try {
            sf::SaveGame::parse(text);
        } catch (const sf::FormatError&) {
            return true;
        }
        return false;
    };
    check(threw("hello"), "junk is rejected");
    check(threw("version=1\n"), "a save without a map is rejected");
    check(threw("version=1\nmap=a\nat=1,x,2\n"), "bad numbers are rejected");
    check(threw("version=1\nmap=a\nclasses=0071\n"), "unknown classes are rejected");
    check(!threw("version=1\nmap=a\nfuture=thing\n"), "unknown keys are skipped");
}

void testWearRepairIdentify() {
    auto sword = kSword;
    sword.durability = 3;
    sword.price = 400;
    auto helmet = kHelmet;
    helmet.durability = 300;
    helmet.price = 1000;
    auto shield = kShield;
    shield.durability = 1;
    auto magic = kSword;
    magic.quality = 1;
    magic.name = "Speed Short Sword";
    magic.unidentifiedName = "Short Sword";

    sf::Item fresh = sf::Item::make(&magic);
    check(fresh.unidentified && fresh.shownName() == "Short Sword", "magic items start unidentified");
    check(!sf::Item::make(&sword).unidentified, "plain items start identified");

    sf::Inventory inventory;
    inventory.equip(Slot::Weapon, {&sword, 1});
    inventory.equip(Slot::Helmet, {&helmet, 1});
    sf::Random random(7);
    std::vector<Slot> broke;
    for (int k = 0; k < 200 && broke.empty(); ++k) broke = inventory.wearFromAttacking(random);
    check(broke == std::vector{Slot::Weapon}, "the weapon wears out from attacking");
    check(inventory.equipped(Slot::Weapon)->broken() && inventory.equipped(Slot::Weapon)->durability() == 0, "and is broken");
    check(inventory.stats()[sf::ItemDef::kAttack] == 0, "broken items add nothing");
    check(inventory.wearFromAttacking(random).empty(), "broken items do not break again");

    int helmetBlows = 0;
    for (int k = 0; k < 1000; ++k) inventory.wearFromBlow(random);
    helmetBlows = inventory.equipped(Slot::Helmet)->wear;
    check(helmetBlows > 120 && helmetBlows < 280, "helmets wear on about one blow in five");

    // A shield behind a two-handed weapon is not hit.
    sf::Inventory twoHanded;
    twoHanded.equip(Slot::Weapon, {&kGreatSword, 1});
    twoHanded.equip(Slot::Shield, {&shield, 1});
    for (int k = 0; k < 100; ++k) twoHanded.wearFromBlow(random);
    check(!twoHanded.equipped(Slot::Shield)->broken(), "hidden shields do not wear");

    // 400 gold, worn 3 of 3: the whole tenth of the price. Helmet: worn
    // share of 100, at least 1.
    check(inventory.repairPrice(0) == 40, "a broken weapon costs a tenth of its price to mend");
    sf::Item lightly{&helmet, 1};
    lightly.wear = 1;
    check(lightly.repairPrice() == 1, "slight wear costs at least 1");
    check(sf::Item{&helmet, 1}.repairPrice() == 0, "unworn items cost nothing");
    check(inventory.repairPrice(1) == helmetBlows * 100 / 300, "the helmet's price is its worn share");
    inventory.repair(0);
    check(!inventory.equipped(Slot::Weapon)->broken() && inventory.stats()[sf::ItemDef::kAttack] == 20, "repair mends the weapon");
    check(inventory.equipped(Slot::Helmet)->wear == helmetBlows, "and only the weapon");

    sf::Item wornSword{&sword, 1};
    wornSword.wear = 2;
    sf::Item wornHelmet{&helmet, 1};
    wornHelmet.wear = 30;
    inventory.addAt(wornSword, 0, 0);
    inventory.addAt(wornHelmet, 2, 0);
    check(inventory.repairPrice(sf::Inventory::kRepairBag) == 2 * 40 / 3 + 10, "the bag's price adds up its items");
    check(inventory.repairPrice(7) == inventory.repairPrice(sf::Inventory::kRepairBag), "other numbers price the bag too");
    inventory.repair(7);
    check(inventory.bag()[0].item.wear == 2, "but repair does nothing for them");
    inventory.repair(sf::Inventory::kRepairBag);
    check(inventory.bag()[0].item.wear == 0 && inventory.bag()[1].item.wear == 0, "repairing the bag mends what is in it");

    check(!inventory.hasUnidentified(), "nothing to identify yet");
    inventory.add(fresh);
    check(inventory.hasUnidentified(), "an unidentified item is found");
    inventory.identifyAll();
    check(!inventory.hasUnidentified() && inventory.bag().back().item.shownName() == "Speed Short Sword", "identify names it");

    // Wear and the unidentified flag are saved; plain lines still load.
    sf::ItemTable table;
    table.items = {sword};
    sf::Inventory before;
    sf::Item kept{&table.items[0], 1};
    kept.wear = 2;
    kept.unidentified = true;
    before.addAt(kept, 0, 0);
    before.equip(Slot::Weapon, kept);
    sf::SaveGame save;
    save.map = "a";
    save.store(before);
    sf::Inventory after;
    sf::SaveGame::parse(save.write()).restore(after, table);
    check(after.bag().size() == 1 && after.bag()[0].item.wear == 2 && after.bag()[0].item.unidentified, "bag items keep their wear");
    check(after.equipped(Slot::Weapon) && after.equipped(Slot::Weapon)->wear == 2 && after.equipped(Slot::Weapon)->unidentified,
          "worn items keep their wear");
    const auto plain = sf::SaveGame::parse("version=1\nmap=a\nbag=0,0,1,0,0\nwear=3,0,0,1\n");
    check(plain.bag.size() == 1 && plain.bag[0].item.wear == 0 && plain.equipped[3] && !plain.equipped[3]->unidentified,
          "older item lines load as new");
}

void testStorageClicks() {
    sf::ItemTable table;
    table.items = {def(Class::Weapon, sf::ItemDef::kSword, 1, 4, 12), def(Class::Consumable, 0, 1, 1)};
    sf::ShopStock storage;
    std::optional<sf::Item> hand = sf::Item{&table.items[0], 1};
    check(storage.click(hand, 8, 0) == sf::Inventory::Result::Done && !hand && storage.goods.size() == 1, "an item is put away");
    check(storage.click(hand, 8, 9) == sf::Inventory::Result::Nothing, "an empty cell gives nothing");
    hand = sf::Item{&table.items[1], 1};
    check(storage.click(hand, 9, 0) == sf::Inventory::Result::NoRoom && hand, "nothing goes off the grid");
    check(storage.click(hand, 8, 2) == sf::Inventory::Result::Done && hand && hand->def == &table.items[0] && storage.goods.size() == 1,
          "putting an item on another swaps them");
    check(storage.click(hand, 0, 0) == sf::Inventory::Result::Done && storage.goods.size() == 2, "the sword goes back");
    check(storage.click(hand, 0, 3) == sf::Inventory::Result::Done && hand && hand->def == &table.items[0], "and is picked up again");
}

}  // namespace

int main() {
    testBagPlacement();
    testBagClicks();
    testEquip();
    testStatsAndLayers();
    testUse();
    testLoot();
    testSaveGame();
    testStorageClicks();
    testWearRepairIdentify();
    if (failures == 0) std::puts("all inventory tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
