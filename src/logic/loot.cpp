#include "logic/loot.h"

#include <algorithm>
#include <cmath>

#include "logic/ai_select.h"

namespace sf {

namespace {

using Class = ItemDef::Class;

struct Allowed {
    bool normal = false, magic = false, unique = false;
};

// Whether an item may be picked (0x465500 and 0x4653a0): its quality is
// allowed (quest items always are), it shares a pool bit, and its item
// level lies in the entry's range.
bool matches(const ItemDef& def, const DropTables::Entry& e, const Allowed& q) {
    const bool quality = (def.quality == 0 && q.normal) || (def.quality == 1 && q.magic) ||
                         (def.quality == 2 && q.unique) || def.quality == 3;
    if (!quality || (def.poolMask & e.pool) == 0) return false;
    if (e.maxLevel != -1 && def.itemLevel > e.maxLevel) return false;
    if (e.minLevel != -1 && def.itemLevel < e.minLevel) return false;
    return true;
}

int weightOf(const ItemTable& items, Class cls, const DropTables::Entry& e, const Allowed& q) {
    int total = 0;
    for (const auto& def : items.items) {
        if (def.cls == cls && matches(def, e, q)) total += def.dropWeight;
    }
    return total;
}

const ItemDef* pickWeighted(const ItemTable& items, Class cls, const DropTables::Entry& e, const Allowed& q, int pick) {
    int sum = 0;
    for (const auto& def : items.items) {
        if (def.cls != cls || !matches(def, e, q)) continue;
        sum += def.dropWeight;
        if (pick < sum) return &def;
    }
    return nullptr;
}

// 0x401520 with 9: nine random decimal digits.
int nineDigits(Random& random) {
    int v = 0;
    for (int i = 0; i < 9; ++i) v = v * 10 + random.next(10);
    return v;
}

// Special items (class 4) are never picked by weight: 0x465500 has no
// level case for them and so counts none.
bool pickable(Class cls) { return cls != Class::Special; }

const ItemDef* pickEntry(const ItemTable& items, const DropTables::Entry& e, const Allowed& q, Random& random) {
    if (e.code != -1) return e.cls >= 0 && e.cls <= 4 ? items.find(static_cast<Class>(e.cls), e.code) : nullptr;
    if (e.cls == -1) {
        // Any of weapons, armour and accessories, weighted together.
        int weights[3], total = 0;
        for (int k = 0; k < 3; ++k) total += weights[k] = weightOf(items, static_cast<Class>(k), e, q);
        if (total == 0) return nullptr;
        int pick = nineDigits(random) % total;
        for (int k = 0; k < 3; ++k) {
            if (weights[k] == 0) continue;
            if (const ItemDef* def = pickWeighted(items, static_cast<Class>(k), e, q, pick)) return def;
            pick -= weights[k];
        }
        return nullptr;
    }
    if (e.cls < 0 || e.cls > 4 || !pickable(static_cast<Class>(e.cls))) return nullptr;
    const int total = weightOf(items, static_cast<Class>(e.cls), e, q);
    if (total == 0) return nullptr;
    return pickWeighted(items, static_cast<Class>(e.cls), e, q, nineDigits(random) % total);
}

}  // namespace

std::vector<const ItemDef*> rollDrops(const DropTables& tables, const ItemTable& items, int number, Random& random,
                                      bool qualityPerRoll) {
    std::vector<const ItemDef*> drops;
    const DropTables::List* list = tables.list(number);
    if (!list || list->picks.empty()) return drops;
    // 0 rolls means one per party member; the hero plays alone.
    const int rolls = list->rolls == 0 ? 1 : list->rolls;
    Allowed q;
    for (int roll = 0; roll < rolls; ++roll) {
        // Note <=: a chance of 15 drops 16 times in 100.
        if (random.next(100) > list->chance) continue;
        const auto& pick = list->picks[static_cast<std::size_t>(random.next(static_cast<int>(list->picks.size())))];
        if (pick.entry < 0 || static_cast<std::size_t>(pick.entry) >= tables.entries.size()) continue;
        const auto& entry = tables.entries[static_cast<std::size_t>(pick.entry)];
        // The exe only ever sets these, starting from none.
        if (qualityPerRoll) q = {};
        if (pick.quality % 10 != 0) q.normal = true;
        if (pick.quality % 100 / 10 != 0) q.magic = true;
        if (pick.quality % 1000 / 100 != 0) q.unique = true;
        // When nothing matches, the exe makes whatever item the last roll
        // left behind (or a stale one); we drop nothing.
        if (const ItemDef* def = pickEntry(items, entry, q, random)) drops.push_back(def);
    }
    return drops;
}

void dropSpot(int i, int count, double x, double y, double& outX, double& outY) {
    const double angle = count > 0 ? i * 6.29184 / count : 0.0;
    outX = x + std::cos(angle) * 200.0;
    outY = y + std::sin(angle) * 200.0;
}

int rollGold(int chance, int min, int max, int bonus, Random& random) {
    const int percent = 100 + bonus;
    if (percent <= 0 || chance <= 0 || random.next(100) >= chance) return 0;
    if (max < min) std::swap(min, max);
    return std::max(1, (min + random.next(max - min + 1)) * percent / 100);
}

}  // namespace sf
