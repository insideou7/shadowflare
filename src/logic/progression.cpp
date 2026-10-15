#include "logic/progression.h"

#include <algorithm>
#include <array>

#include "formats/table_data.h"

namespace sf {
namespace {

constexpr std::int64_t kTopOfTable = 999999999;

constexpr std::array<std::int64_t, 99> kExperiencePerLevel = {
    25, 80, 150, 200, 300, 400, 600, 1200, 2000, 2200,
    2400, 3000, 6000, 20000, 25000, 30000, 40000, 50000, 60000, 120000,
    150000, 200000, 250000, 300000, 400000, 500000, 600000, 800000, 1000000, 1200000,
    1400000, 1600000, 1800000, 2000000, 2200000, 2500000, 2800000, 3100000, 3400000, 3700000,
    4000000, 4300000, 4600000, 4900000, 5200000, 5500000, 5800000, 6100000, 6400000, 6900000,
    7400000, 7900000, 8400000, 8900000, 9400000, 9900000, 11000000, 11000000, 11000000, 11000000,
    12000000, 12000000, 12000000, 12000000, 13000000, 13000000, 13000000, 13000000, 13000000, 15000000,
    15000000, 15000000, 15000000, 15000000, 20000000, 20000000, 20000000, 20000000, 20000000, 25000000,
    40000000, 50000000, 60000000, 70000000, 80000000, 100000000, 150000000, 200000000, 250000000, 300000000,
    350000000, 400000000, 450000000, 500000000, 550000000, 600000000, 650000000, 700000000, kTopOfTable,
};

// Table.Tbd table 900, column 0, and what columns 1-4 add; table 901 for
// the heroine.
constexpr std::array<int, HeroStats::kCount> kStartingStats = {100, 128, 150, 150, 140, 10, 10, 10, 10, 40, 40, 40, 40};
constexpr std::array<int, HeroStats::kCount> kClasslessGain = {0, 0, 40, 0, 10, 4, 4, 2, 2, 0, 0, 0, 0};
constexpr std::array<int, HeroStats::kCount> kHeroineStartingStats = {100, 128, 140, 160, 140, 8, 12, 10, 10, 40, 40, 40, 40};
constexpr std::array<int, HeroStats::kCount> kHeroineClasslessGain = {0, 0, 40, 0, 10, 3, 5, 2, 2, 0, 0, 0, 0};

// What each level in a class adds, as runs of (amount, levels); the last
// run lasts for the rest. Tables 902, 904 and 906.
struct Run {
    int amount, levels;
};
using Gains = std::array<std::vector<Run>, HeroStats::kCount>;

const Gains kWarriorGain = {{
    {}, {},
    {{50, 20}, {40, 10}, {30, 0}},
    {{10, 10}, {8, 10}, {6, 10}, {5, 0}},
    {{10, 6}, {9, 14}, {8, 0}},
    {{9, 10}, {7, 10}, {6, 0}},
    {{6, 10}, {5, 10}, {4, 0}},
    {{1, 0}},
    {{3, 10}, {2, 20}, {1, 0}},
    {}, {}, {}, {},
}};
const Gains kMagicianGain = {{
    {}, {},
    {{15, 20}, {10, 0}},
    {{50, 20}, {40, 10}, {30, 0}},
    {{4, 5}, {3, 10}, {2, 10}, {1, 0}},
    {{1, 0}},
    {{3, 10}, {2, 0}},
    {{12, 10}, {10, 10}, {8, 0}},
    {{12, 10}, {10, 10}, {8, 0}},
    {}, {},
    {{1, 0}},
    {},
}};
const Gains kHunterGain = {{
    {},  // attack speed: +1 every second level, handled in heroStats
    {{1, 0}},
    {{25, 10}, {20, 10}, {15, 0}},
    {{25, 10}, {20, 10}, {15, 0}},
    {{7, 5}, {5, 1}, {6, 4}, {5, 5}, {4, 10}, {3, 15}, {2, 0}},
    {{6, 10}, {5, 10}, {4, 10}, {3, 0}},
    {{4, 0}},
    {{8, 10}, {7, 10}, {6, 10}, {5, 0}},
    {{6, 19}, {5, 11}, {4, 0}},
    {{1, 0}},
    {}, {}, {},
}};

// Table 905, the witch: as table 904 but for health, 15 for 25 levels, and
// magic, 50 for 21 levels. The warrior's and hunter's tables (903, 907) are
// the same as the hero's.
const Gains kWitchGain = [] {
    Gains gains = kMagicianGain;
    gains[HeroStats::kHp] = {{15, 25}, {10, 0}};
    gains[HeroStats::kMp] = {{50, 21}, {40, 9}, {30, 0}};
    return gains;
}();

int gainAt(const std::vector<Run>& runs, int column) {
    for (const auto& run : runs) {
        if (run.levels == 0 || column < run.levels) return run.amount;
        column -= run.levels;
    }
    return 0;
}

}  // namespace

LevelCurve LevelCurve::fromTables(const GameTables& tables) {
    LevelCurve curve;
    const auto* table = tables.find(13);
    if (!table || table->cols < 1) return curve;
    for (int row = 0; row < table->rows; ++row) {
        const std::int64_t need = table->value(row, 0);
        if (need <= 0) break;
        // The last row, 999999999, is what level 99 takes: a quest reward
        // can still pay it (the script command at 0x430f80 case 0x44), and level 100 shows Max.
        curve.needs.push_back(need);
        if (need >= kTopOfTable) break;
    }
    return curve;
}

LevelCurve LevelCurve::standard() {
    return {std::vector<std::int64_t>(kExperiencePerLevel.begin(), kExperiencePerLevel.end())};
}

std::int64_t LevelCurve::need(int level) const {
    if (level < 1 || level >= maxLevel()) return 0;
    return needs[static_cast<std::size_t>(level - 1)];
}

std::string_view className(HeroClass cls, HeroSex sex) {
    switch (cls) {
        case HeroClass::Mercenary: return "Mercenary";
        case HeroClass::Warrior: return "Warrior";
        case HeroClass::Hunter: return "Hunter";
        case HeroClass::Wizard: return sex == HeroSex::Female ? "Witch" : "Wizard";
    }
    return "";
}

HeroStats heroStats(const std::vector<HeroClass>& history, HeroSex sex) {
    const bool heroine = sex == HeroSex::Female;
    const auto& classless = heroine ? kHeroineClasslessGain : kClasslessGain;
    const Gains& wizard = heroine ? kWitchGain : kMagicianGain;
    HeroStats stats;
    stats.value = heroine ? kHeroineStartingStats : kStartingStats;
    std::array<int, kHeroClassCount> levels{};
    levels[static_cast<int>(HeroClass::Mercenary)] = 1;  // the new hero's column 0
    for (const HeroClass cls : history) {
        const int column = levels[static_cast<int>(cls)]++;
        for (int s = 0; s < HeroStats::kCount; ++s) {
            switch (cls) {
                case HeroClass::Mercenary: stats.value[s] += column <= 4 ? classless[s] : 0; break;
                case HeroClass::Warrior: stats.value[s] += gainAt(kWarriorGain[s], column); break;
                case HeroClass::Hunter: stats.value[s] += gainAt(kHunterGain[s], column); break;
                case HeroClass::Wizard: stats.value[s] += gainAt(wizard[s], column); break;
            }
        }
        // The hunter's attack speed rises every second level (odd columns).
        if (cls == HeroClass::Hunter && column % 2 == 1) ++stats.value[HeroStats::kAttackSpeed];
    }
    return stats;
}

std::vector<HeroClass> classHistory(int level, HeroClass cls) {
    std::vector<HeroClass> history;
    for (int l = 1; l < level; ++l) history.push_back(l < 5 ? HeroClass::Mercenary : cls);
    return history;
}

Career Career::at(int level, HeroClass cls) {
    Career career;
    career.history = classHistory(level, cls);
    if (level >= 5) career.current = career.offered = cls;
    return career;
}

void Career::countKill(std::optional<int> weaponSubtype, bool magic) {
    ++kills;
    // Staffs (8) and rods (9) count as magic; bare hands count only in the
    // total.
    if (magic || (weaponSubtype && *weaponSubtype >= kMagic)) {
        ++killsBy[kMagic];
    } else if (weaponSubtype && *weaponSubtype >= 0) {
        ++killsBy[static_cast<std::size_t>(*weaponSubtype)];
    }
}

void Career::levelUp(int level) {
    history.push_back(current);
    if (level >= 5) {
        std::array<int, 9> percent{};
        for (std::size_t i = 0; i < percent.size(); ++i) percent[i] = kills ? killsBy[i] * 100 / kills : 0;
        bool found = false;
        if (current != HeroClass::Wizard && percent[kMagic] >= 20) {
            offered = HeroClass::Wizard;
            found = true;
        }
        if (current != HeroClass::Hunter && !found && percent[5] >= 30) {
            offered = HeroClass::Hunter;
            found = true;
        }
        // One- and two-handed swords, axes and maces.
        if (!found) offered = current != HeroClass::Warrior && percent[0] + percent[1] + percent[3] >= 50 ? HeroClass::Warrior : current;
        if (level == 5) current = offered = HeroClass::Warrior;
    }
    kills = 0;
    killsBy = {};
}

bool Career::change() {
    if (!canChange()) return false;
    current = offered;
    return true;
}

int hitChance(int hit, int evasion) { return std::clamp(hit - evasion, 20, 98); }

int meleeDamage(int attack, int defense, int roll) {
    return std::max(1, attack * (9 + std::clamp(roll, 0, 2)) / 10 - defense);
}

int defaultExperience(int hp, int attack) { return std::max(1, hp / 3 + attack); }

}  // namespace sf
