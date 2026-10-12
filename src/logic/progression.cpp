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

LevelStats statsAtLevel(int level) {
    const int gained = std::max(0, level - 1);
    int hp = 150 + 40 * std::min(gained, 4);
    for (int k = 1; k <= level - 5; ++k) hp += k <= 20 ? 50 : k <= 30 ? 40 : 30;
    return {hp, 2 * gained, gained};
}

int defaultExperience(int hp, int attack) { return std::max(1, hp / 3 + attack); }

}  // namespace sf
