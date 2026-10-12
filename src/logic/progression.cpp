#include "logic/progression.h"

#include <algorithm>
#include <array>

#include "formats/table_data.h"

namespace sf {
namespace {

constexpr std::int64_t kTopOfTable = 999999999;

constexpr std::array<std::int64_t, 98> kExperiencePerLevel = {
    25, 80, 150, 200, 300, 400, 600, 1200, 2000, 2200,
    2400, 3000, 6000, 20000, 25000, 30000, 40000, 50000, 60000, 120000,
    150000, 200000, 250000, 300000, 400000, 500000, 600000, 800000, 1000000, 1200000,
    1400000, 1600000, 1800000, 2000000, 2200000, 2500000, 2800000, 3100000, 3400000, 3700000,
    4000000, 4300000, 4600000, 4900000, 5200000, 5500000, 5800000, 6100000, 6400000, 6900000,
    7400000, 7900000, 8400000, 8900000, 9400000, 9900000, 11000000, 11000000, 11000000, 11000000,
    12000000, 12000000, 12000000, 12000000, 13000000, 13000000, 13000000, 13000000, 13000000, 15000000,
    15000000, 15000000, 15000000, 15000000, 20000000, 20000000, 20000000, 20000000, 20000000, 25000000,
    40000000, 50000000, 60000000, 70000000, 80000000, 100000000, 150000000, 200000000, 250000000, 300000000,
    350000000, 400000000, 450000000, 500000000, 550000000, 600000000, 650000000, 700000000,
};

// Table.Tbd table 900, column 0, and what columns 1-4 add.
constexpr std::array<int, HeroStats::kCount> kStartingStats = {100, 128, 150, 150, 140, 10, 10, 10, 10, 40, 40, 40, 40};
constexpr std::array<int, HeroStats::kCount> kClasslessGain = {0, 0, 40, 0, 10, 4, 4, 2, 2, 0, 0, 0, 0};

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
        if (need <= 0 || need >= kTopOfTable) break;
        curve.needs.push_back(need);
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

HeroClass classAtLevel(int level) { return level <= 5 ? HeroClass::Novice : HeroClass::Warrior; }

HeroStats heroStats(int level, HeroClass cls) {
    HeroStats stats;
    stats.value = kStartingStats;
    for (int l = 2; l <= std::min(level, 5); ++l) {
        for (int s = 0; s < HeroStats::kCount; ++s) stats.value[s] += kClasslessGain[s];
    }
    if (cls == HeroClass::Novice) return stats;
    const auto& gains = cls == HeroClass::Warrior ? kWarriorGain : cls == HeroClass::Magician ? kMagicianGain : kHunterGain;
    for (int column = 0; column < level - 5; ++column) {
        for (int s = 0; s < HeroStats::kCount; ++s) stats.value[s] += gainAt(gains[s], column);
    }
    // The hunter's attack speed rises every second level (odd columns).
    if (cls == HeroClass::Hunter) stats.value[HeroStats::kAttackSpeed] += std::max(0, level - 5) / 2;
    return stats;
}

int hitChance(int hit, int evasion) { return std::clamp(hit - evasion, 20, 98); }

int meleeDamage(int attack, int defense, int roll) {
    return std::max(1, attack * (9 + std::clamp(roll, 0, 2)) / 10 - defense);
}

int defaultExperience(int hp, int attack) { return std::max(1, hp / 3 + attack); }

}  // namespace sf
