#include "formats/drop_tables.h"

#include "formats/table_data.h"

namespace sf {

const DropTables::List* DropTables::list(int number) const {
    const auto it = lists.find(number);
    return it == lists.end() ? nullptr : &it->second;
}

DropTables DropTables::fromTables(const GameTables& tables) {
    DropTables d;
    const auto* lists = tables.find(30);
    const auto* entries = tables.find(31);
    if (!lists || !entries || lists->cols < 2 || entries->cols < 5) return d;
    for (int r = 0; r < entries->rows; ++r) {
        d.entries.push_back({entries->value(r, 0), entries->value(r, 1), entries->value(r, 2), entries->value(r, 3),
                             entries->value(r, 4)});
    }
    for (int r = 0; r < lists->rows; ++r) {
        List& l = d.lists[r];
        l.rolls = lists->value(r, 0);
        l.chance = lists->value(r, 1);
        for (int c = 2; c + 1 < lists->cols; c += 2) l.picks.push_back({lists->value(r, c), lists->value(r, c + 1)});
    }
    return d;
}

}  // namespace sf
