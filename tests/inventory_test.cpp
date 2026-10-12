// Tests for the inventory and equipment rules (src/logic/inventory.h).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

#include "logic/ai_select.h"
#include "formats/binary_reader.h"
#include "logic/inventory.h"
#include "logic/loot.h"
#include "logic/partner.h"
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
const sf::ModifierPrices kNoPrices{};
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
        const int gold = sf::rollGold(10, 5, 8, 0, random);
        check(gold == 0 || (gold >= 5 && gold <= 8), "gold amount in range");
        drops += gold > 0;
    }
    check(drops > 50 && drops < 150, "gold drops about 10% of the time");
    check(sf::rollGold(0, 5, 8, 0, random) == 0, "no chance, no gold");
    check(sf::rollGold(100, 10, 10, 50, random) == 15, "the Amount of Gold bonus raises it");
    check(sf::rollGold(100, 10, 10, -100, random) == 0, "a bonus of -100% stops gold");

    // Table 31 rows: 0 a Tablet; 1 any weapon, armour or accessory of item
    // level 1-2 in pool 1; 2 any of level 30.
    sf::ItemTable table;
    table.items = {kSword, kHeavy, kGold, kTablet};
    table.items[0].poolMask = 1;
    table.items[0].dropWeight = 5;
    table.items[1].poolMask = 1;
    table.items[1].dropWeight = 5;
    table.items[1].itemLevel = 30;
    table.items[3].code = 0;
    sf::DropTables tables;
    tables.entries = {{3, -1, -1, 0, 1}, {-1, 2, 1, -1, 1}, {-1, 30, 30, -1, 1}};
    tables.lists[0] = {1, 100, {{0, 111}}, std::nullopt};
    tables.lists[1] = {1, 100, {{1, 111}}, std::nullopt};
    tables.lists[2] = {2, 0, {{0, 111}}, std::nullopt};
    tables.lists[3] = {1, 100, {{-1, 111}}, std::nullopt};
    tables.lists[4] = {1, 100, {{1, 10}}, std::nullopt};
    for (int i = 0; i < 50; ++i) {
        auto d = sf::rollDrops(tables, table, 0, random, true);
        check(d.size() == 1 && d[0] == &table.items[3], "a set code drops that item");
        d = sf::rollDrops(tables, table, 1, random, true);
        check(d.size() == 1 && d[0] == &table.items[0], "a weighted pick keeps to the level range");
        check(sf::rollDrops(tables, table, 3, random, true).empty(), "a -1 pick drops nothing");
        check(sf::rollDrops(tables, table, 4, random, true).empty(), "normal items need the ones digit");
    }
    // A chance of 0 still drops 1 time in 100, as the exe compares with <=.
    int lucky = 0;
    for (int i = 0; i < 2000; ++i) lucky += !sf::rollDrops(tables, table, 2, random, true).empty();
    check(lucky > 5 && lucky < 80, "chance 0 drops about 1% of the time per roll");
    check(sf::rollDrops(tables, table, 9, random, true).empty(), "no such list");
    table.items[0].poolMask = 2;
    check(sf::rollDrops(tables, table, 1, random, true).empty(), "the pool masks must share a bit");

    // The original's slip: one roll allowing magic lets later rolls drop it.
    sf::ItemTable magic;
    magic.items = {kSword, kSword};
    magic.items[0].poolMask = magic.items[1].poolMask = 1;
    magic.items[0].dropWeight = magic.items[1].dropWeight = 1;
    magic.items[1].code = 1;
    magic.items[1].quality = 1;
    sf::DropTables two;
    two.entries = {{0, -1, -1, -1, 1}};
    two.lists[0] = {2, 100, {{0, 11}, {0, 1}}, std::nullopt};
    magic.items[1].dropWeight = 1000;
    int magicDrops[2] = {};  // ours, original
    for (const bool ours : {true, false}) {
        sf::Random r(11);
        for (int i = 0; i < 2000; ++i) {
            for (const sf::ItemDef* d : sf::rollDrops(two, magic, 0, r, ours)) magicDrops[ours ? 0 : 1] += d->quality == 1;
        }
    }
    // Ours: only the "11" picks give magic, about half the rolls. The
    // original: also every "1" pick after one.
    check(magicDrops[0] > 1700 && magicDrops[0] < 2300, "only rolls allowing magic give it");
    check(magicDrops[1] > magicDrops[0] + 500, "original mode lets magic carry over between rolls");

    double x, y;
    sf::dropSpot(0, 2, 1000, 1000, x, y);
    check(std::abs(x - 1200) < 1e-6 && std::abs(y - 1000) < 1e-6, "the first item lands 200 to the side");
}

void testBelt() {
    sf::ItemDef tablet = kTablet;
    tablet.effect[0] = 1;  // may go in the belt
    sf::ItemDef capsule = kTablet;
    capsule.code = 10000000;
    capsule.effect[0] = 1;
    sf::ItemDef herb = kTablet;  // usable, but not put in the belt by itself
    herb.code = 5;

    sf::Inventory inv;
    for (int i = 0; i < 4; ++i) check(inv.addToBelt({&tablet}), "four tablets fit the top row");
    check(inv.beltAt(3, 0) && !inv.beltAt(0, 1), "the belt fills row by row");
    check(inv.pickUp({&capsule}) && inv.beltAt(0, 1) && inv.bag().empty(), "a picked-up capsule goes in the belt");
    check(inv.pickUp({&herb}) && inv.bag().size() == 1 && !inv.beltAt(1, 1), "other consumables go in the bag");
    check(inv.pickUp({&kSword}) && inv.bag().size() == 2, "so does everything else");

    // Using a belt item takes a like one from the bag first.
    inv.add({&tablet});
    inv.useUpBelt(0, 0);
    check(inv.beltAt(0, 0) && inv.bag().size() == 2, "a spare tablet in the bag is used instead");
    inv.useUpBelt(0, 0);
    check(!inv.beltAt(0, 0), "without a spare, the belt's own is used");

    std::optional<sf::Item> hand;
    check(inv.clickBelt(hand, 1, 0) == Result::Done && hand && !inv.beltAt(1, 0), "a click picks it up");
    check(inv.clickBelt(hand, 0, 1) == Result::Done && hand && hand->def == &capsule && inv.beltAt(0, 1)->def == &tablet,
          "putting it on another swaps them");
    std::optional<sf::Item> sword{sf::Item{&kSword}};
    check(inv.clickBelt(sword, 2, 1) == Result::WrongSlot && sword, "a sword does not go in the belt");
    sf::ItemDef bigPotion = kTablet;
    bigPotion.height = 2;
    check(!inv.addToBelt({&bigPotion}), "only 1 x 1 items fit");
    inv.clear();
    check(!inv.beltAt(1, 1) && !inv.beltAt(3, 0), "clear empties the belt");
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
    check(inventory.addToBeltAt({&table.items[1], 1}, 2, 1), "potion goes in the belt");

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
    save.sex = sf::HeroSex::Female;
    save.name = "Ada, of the Hills";

    const auto loaded = sf::SaveGame::parse(save.write());
    check(loaded.sex == sf::HeroSex::Female && loaded.name == "Ada, of the Hills", "the heroine and her name survive");
    check(loaded.map == "waterworks" && loaded.scenario == "Scenario/00000002", "map and scenario survive");
    check(loaded.x == 1280 && loaded.y == 1472 && loaded.direction == 5, "position survives");
    check(loaded.hp == 92 && loaded.level == 3 && loaded.experience == 140 && loaded.gold == 120, "stats survive");
    check(loaded.flags.at({sf::ScenarioScript::kSave, 71}) == 1, "story flags survive");
    {
        sf::Inventory back;
        loaded.restore(back, table);
        check(back.beltAt(2, 1) && back.beltAt(2, 1)->def == &table.items[1] && !back.beltAt(0, 0), "the belt survives");
    }
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
    check(!older.partner, "older saves have no companion");
    check(older.sex == sf::HeroSex::Male && older.name.empty(), "older saves are the hero's");

    save.partner.emplace();
    save.partner->kind = 4;
    save.partner->active = true;
    save.partner->reviveTicks = 120;
    save.partner->levels[4] = 7;
    save.partner->experience[4] = 3;
    save.partner->levels[0] = 2;
    save.partnerHp = 0;
    const auto withPartner = sf::SaveGame::parse(save.write());
    check(withPartner.partner && withPartner.partner->kind == 4 && withPartner.partner->active &&
              withPartner.partner->reviveTicks == 120 && withPartner.partner->levels[4] == 7 &&
              withPartner.partner->experience[4] == 3 && withPartner.partner->levels[0] == 2 && withPartner.partnerHp == 0,
          "the companion, every kind's level and its revival survive");

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
    check(loaded.restore(partial, table) == 2, "unknown items are skipped, in the bag and the belt");

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
    check(inventory.repairPrice(0, kNoPrices) == 40, "a broken weapon costs a tenth of its price to mend");
    sf::Item lightly{&helmet, 1};
    lightly.wear = 1;
    check(lightly.repairPrice(kNoPrices) == 1, "slight wear costs at least 1");
    check(sf::Item{&helmet, 1}.repairPrice(kNoPrices) == 0, "unworn items cost nothing");
    check(inventory.repairPrice(1, kNoPrices) == helmetBlows * 100 / 300, "the helmet's price is its worn share");
    inventory.repair(0);
    check(!inventory.equipped(Slot::Weapon)->broken() && inventory.stats()[sf::ItemDef::kAttack] == 20, "repair mends the weapon");
    check(inventory.equipped(Slot::Helmet)->wear == helmetBlows, "and only the weapon");

    sf::Item wornSword{&sword, 1};
    wornSword.wear = 2;
    sf::Item wornHelmet{&helmet, 1};
    wornHelmet.wear = 30;
    inventory.addAt(wornSword, 0, 0);
    inventory.addAt(wornHelmet, 2, 0);
    check(inventory.repairPrice(sf::Inventory::kRepairBag, kNoPrices) == 2 * 40 / 3 + 10, "the bag's price adds up its items");
    check(inventory.repairPrice(7, kNoPrices) == inventory.repairPrice(sf::Inventory::kRepairBag, kNoPrices), "other numbers price the bag too");
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

// Magic modifiers: rolled per item, counted in the stats, the companion's
// stats and the price (0x467310, 0x44eb5b, 0x4138b0, 0x4661c0).
void testModifiers() {
    using M = sf::ItemDef;
    auto magic = def(Class::Weapon, sf::ItemDef::kSword, 1, 4, 12);
    magic.price = 400;
    magic.durability = 100;
    magic.modifiers[M::kModAttack] = {10, 30, 100};
    magic.modifiers[M::kModHighDamage] = {1, 1, 100};
    magic.modifiers[M::kModCompanionHp] = {100, 100, 100};
    magic.modifiers[M::kModCompanionDefense] = {-50, -50, 100};
    magic.modifiers[M::kModMaxMp] = {5, 9, 0};  // never rolls
    magic.elementModifiers[0] = {2, 2, 100};
    sf::Random random(3);
    for (int k = 0; k < 50; ++k) {
        const auto item = sf::Item::make(&magic, random);
        const int attack = item.modifiers[M::kModAttack];
        check(attack >= 10 && attack <= 30, "a modifier rolls within its range");
        check(item.modifiers[M::kModMaxMp] == 0, "a modifier without a chance stays 0");
    }
    sf::Item item = sf::Item::make(&magic, random);
    item.modifiers[M::kModAttack] = 20;
    check(item.elementModifiers[0] == 2, "element modifiers roll too");
    check(sf::Item::make(&magic).modifiers[M::kModAttack] == 0, "items made without a roll have none");

    // Table 34 prices: 100 a point of element, 20 a point of attack, 1000
    // for the high-damage modifier counted from 1 on weapons.
    sf::ModifierPrices prices;
    prices.element[0] = 100;
    prices.modifier[M::kModAttack] = 20;
    prices.modifier[M::kModHighDamage] = 1000;
    check(item.value(prices) == 400 + 2 * 100 + 20 * 20, "modifiers raise the value");
    check(item.value(kNoPrices) == 400, "without the table only the price counts");
    item.wear = 50;
    check(item.repairPrice(prices) == 50 * (1000 / 10) / 100, "mending costs a tenth of the value, by wear");
    check(item.repairPrice(kNoPrices) == 50 * 40 / 100, "a plain price mends for less");
    check(item.sellValue(prices) == 50 * 1000 / 100 / 4, "a shop pays a quarter of the value left");
    auto fixed = magic;
    fixed.fixedPrice = true;
    sf::Item fixedItem = item;
    fixedItem.def = &fixed;
    check(fixedItem.value(prices) == 400, "a fixed price ignores modifiers");

    sf::Inventory worn;
    item.wear = 0;
    worn.equip(Slot::Weapon, item);
    check(worn.stats()[M::kAttack] == 20, "the attack modifier adds to attack");
    check(worn.modifier(M::kModCompanionHp) == 100, "modifiers of worn items add up");
    check(worn.elements()[0] == 2, "element modifiers add to the element levels");

    sf::PartnerStats dog;
    dog.maxHp = 400;
    dog.defense = 30;
    dog.evasion = 0;
    sf::addItemBonuses(dog, worn);
    check(dog.maxHp == 500, "companion HP rises with the hero's items");
    check(dog.defense == 1, "companion stats stay at least 1");
    check(dog.evasion == 1, "even ones the items do not touch");

    // Broken weapons stop counting, and so does a shield behind a
    // two-handed weapon.
    sf::Item broken = item;
    broken.wear = magic.durability;
    sf::Inventory brokenSet;
    brokenSet.equip(Slot::Weapon, broken);
    check(brokenSet.modifier(M::kModCompanionHp) == 0, "broken weapons add nothing");
    auto shieldDef = def(Class::Armour, sf::ItemDef::kShield, 2, 2, 9);
    shieldDef.modifiers[M::kModCompanionHp] = {7, 7, 100};
    sf::Inventory hidden;
    hidden.equip(Slot::Weapon, {&kGreatSword, 1});
    hidden.equip(Slot::Shield, sf::Item::make(&shieldDef, random));
    check(hidden.modifier(M::kModCompanionHp) == 0, "a hidden shield adds nothing");

    // Saves keep the rolls.
    sf::Inventory inv;
    inv.addAt(item, 0, 0);
    sf::SaveGame save;
    save.map = "m";
    save.store(inv);
    const auto loaded = sf::SaveGame::parse(save.write());
    sf::ItemTable table;
    table.items.push_back(magic);
    table.items.back().code = 0;
    sf::Inventory back;
    loaded.restore(back, table);
    check(back.bag().size() == 1 && back.bag()[0].item.modifiers == item.modifiers &&
              back.bag()[0].item.elementModifiers == item.elementModifiers,
          "saved items keep their modifiers");
}

}  // namespace

int main() {
    testBagPlacement();
    testBagClicks();
    testEquip();
    testStatsAndLayers();
    testUse();
    testLoot();
    testBelt();
    testSaveGame();
    testStorageClicks();
    testWearRepairIdentify();
    testModifiers();
    if (failures == 0) std::puts("all inventory tests passed");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
