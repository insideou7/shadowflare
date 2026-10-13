#include "logic/partner.h"

#include <algorithm>

#include "formats/table_data.h"
#include "logic/inventory.h"
#include "logic/magic.h"

namespace sf {

namespace {

constexpr int kKindTable = 60;
constexpr int kStatTableBase = 800;
constexpr int kStatRows = 19;

}  // namespace

std::optional<PartnerTables> PartnerTables::load(const GameTables& tables) {
    const auto* kinds = tables.find(kKindTable);
    if (!kinds || kinds->rows < kPartnerKinds || kinds->cols < 5) return std::nullopt;
    PartnerTables t;
    t.tables_ = &tables;
    for (int k = 0; k < kPartnerKinds; ++k) {
        const auto* stats = tables.find(kStatTableBase + k);
        if (!stats || stats->rows < kStatRows || stats->cols < 1) return std::nullopt;
        auto& kind = t.kinds_[static_cast<std::size_t>(k)];
        if (kinds->strings.size() > static_cast<std::size_t>(k * kinds->cols)) kind.name = kinds->string(k, 0);
        kind.folder = kinds->value(k, 1);
        for (int c = 0; c < 3; ++c) kind.tint[static_cast<std::size_t>(c)] = kinds->value(k, 2 + c);
    }
    return t;
}

PartnerStats PartnerTables::stats(int kind, int level) const {
    const auto& t = *tables_->find(kStatTableBase + kind);
    const int columns = std::clamp(level, 1, t.cols);
    auto sum = [&](int row) {
        int total = 0;
        for (int c = 0; c < columns; ++c) total += t.value(row, c);
        return total;
    };
    PartnerStats s;
    s.attackSpeed = sum(0);
    s.walkSpeed = sum(1);
    s.runSpeed = sum(2);
    s.maxHp = std::max(1, sum(3));
    s.attack = sum(5);
    s.hit = sum(6);
    s.defense = sum(7);
    s.evasion = sum(8);
    s.magicAttack = sum(9);
    s.magicDefense = sum(10);
    s.magicHit = sum(11);
    s.magicEvasion = sum(12);
    s.element = t.value(13, columns - 1);
    s.jumpStunChance = t.value(14, columns - 1);
    s.jumpStunLength = t.value(15, columns - 1);
    s.jumpStunUncapped = t.value(16, columns - 1) != 0;
    s.jumpArea = t.value(17, columns - 1);
    return s;
}

void addItemBonuses(PartnerStats& s, const Inventory& worn) {
    auto add = [&](int& stat, ItemDef::Modifier m) { stat = std::max(1, stat + worn.modifier(m)); };
    add(s.maxHp, ItemDef::kModCompanionHp);
    add(s.attack, ItemDef::kModCompanionAttack);
    add(s.hit, ItemDef::kModCompanionHit);
    add(s.defense, ItemDef::kModCompanionDefense);
    add(s.evasion, ItemDef::kModCompanionEvasion);
    add(s.magicAttack, ItemDef::kModCompanionMagicAttack);
    add(s.magicHit, ItemDef::kModCompanionMagicHit);
    add(s.magicDefense, ItemDef::kModCompanionMagicDefense);
    add(s.magicEvasion, ItemDef::kModCompanionMagicEvasion);
}

void applyMoon(PartnerStats& s, const MagicTables& t, int level) {
    const std::size_t col = MagicTables::column(level);
    // Table 200 row r changes the stat of the companion's table row r - 1;
    // row 4 (row 5 here) is a stat the companion does not use.
    int unused = 1;
    int* const stats[13] = {&s.attackSpeed, &s.walkSpeed, &s.runSpeed, &s.maxHp, &unused, &s.attack, &s.hit,
                            &s.defense, &s.evasion, &s.magicAttack, &s.magicDefense, &s.magicHit, &s.magicEvasion};
    for (std::size_t r = 0; r < 13; ++r) {
        int& v = *stats[r];
        v += t.moon[r + 1][col] * v / 100;
        v = r < 3 ? std::clamp(v, 0, 255) : std::max(v, 1);
    }
}

int PartnerTables::need(int kind, int level) const {
    const auto& t = *tables_->find(kStatTableBase + kind);
    return t.value(18, std::clamp(level, 1, t.cols) - 1);
}

int partnerLevelCap(int heroLevel) { return std::min(kPartnerMaxLevel, heroLevel / 3 + 2); }

bool PartnerRecord::countKill(const PartnerTables& tables, int heroLevel) {
    auto& level = levels[static_cast<std::size_t>(kind)];
    auto& exp = experience[static_cast<std::size_t>(kind)];
    const int cap = partnerLevelCap(heroLevel);
    if (level >= cap) {
        exp = 0;
        return false;
    }
    ++exp;
    bool up = false;
    while (level < cap) {
        const int need = tables.need(kind, level);
        if (need <= 0 || exp < need) break;
        exp -= need;
        ++level;
        up = true;
    }
    if (level >= cap) exp = 0;
    return up;
}

}  // namespace sf
