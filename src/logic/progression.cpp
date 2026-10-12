#include "logic/progression.h"

#include <algorithm>

#include "formats/table_data.h"

namespace sf {

LevelCurve LevelCurve::fromTables(const GameTables& tables) {
    LevelCurve curve;
    const auto* table = tables.find(13);
    if (!table || table->cols < 1) return curve;
    // The list ends where the totals stop rising (it closes with 999999999
    // and 0).
    for (int row = 0; row < table->rows; ++row) {
        const std::int64_t total = table->value(row, 0);
        if (!curve.thresholds.empty() && total <= curve.thresholds.back()) break;
        if (total <= 0 || total >= 999999999) break;
        curve.thresholds.push_back(total);
    }
    return curve;
}

LevelCurve LevelCurve::standard() {
    LevelCurve curve;
    for (std::int64_t level = 1; level < 100; ++level) curve.thresholds.push_back(30 * level * level);
    return curve;
}

int LevelCurve::levelFor(std::int64_t experience) const {
    const auto reached = std::upper_bound(thresholds.begin(), thresholds.end(), experience) - thresholds.begin();
    return static_cast<int>(reached) + 1;
}

std::int64_t LevelCurve::start(int level) const {
    if (level <= 1 || thresholds.empty()) return 0;
    return thresholds[static_cast<std::size_t>(std::min(level, maxLevel()) - 2)];
}

LevelStats statsAtLevel(int level) {
    const int gained = std::max(0, level - 1);
    return {100 + 12 * gained, 2 * gained, gained};
}

int defaultExperience(int hp, int attack) { return std::max(1, hp / 3 + attack); }

}  // namespace sf
