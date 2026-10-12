#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace sf {

struct GameTables;

// What monsters leave behind, as ShadowFlare.exe decides it (0x417550).
// Each monster names a drop list (Mct enemy word 14, monster + 0x180), a
// row of Table.Tbd table 30:
//   col 0  how many rolls (0 = one per party member),
//   col 1  each roll's chance, percent,
//   cols 2-21  ten picks: a table 31 row (-1 = nothing) and which
//          qualities may come, as decimal digits: ones normal, tens magic,
//          hundreds unique ("111" any, "11" no unique).
// A table 31 row is an item class (-1 = weapon, armour or accessory),
// the highest and lowest item level (-1 = no bound), an item code (-1 =
// pick one by drop weight) and a drop pool mask that the item's pool
// (Item.Ibn 0x88) must share a bit with. The script command
// CREATEITEMTABLE drops a list's items too.
struct DropTables {
    struct Pick {
        int entry = -1;     // a table 31 row, -1 = nothing
        int quality = 111;  // the digits above
    };
    struct List {
        int rolls = 1;
        int chance = 0;
        std::vector<Pick> picks;  // the original always has ten
        // Gold for monsters that give none of their own: chance, least,
        // most. Only drops.txt sets it; the original's gold is per monster.
        std::optional<std::array<int, 3>> gold;
    };
    struct Entry {
        int cls = -1;  // ItemDef::Class, or -1
        int maxLevel = -1, minLevel = -1;
        std::int32_t code = -1;
        int pool = 1;
    };
    std::map<int, List> lists;
    std::vector<Entry> entries;

    bool empty() const { return lists.empty(); }
    const List* list(int number) const;
    // Tables 30 and 31; empty when either is missing.
    static DropTables fromTables(const GameTables& tables);
};

}  // namespace sf
