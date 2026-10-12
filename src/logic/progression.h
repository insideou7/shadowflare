#pragma once

#include <cstdint>
#include <vector>

namespace sf {

struct GameTables;

// How much experience the player needs for each level. thresholds[i] is
// the total experience at which level i + 2 begins (level 1 needs none).
struct LevelCurve {
    std::vector<std::int64_t> thresholds;

    // The original's curve: Table.Tbd table 13, 100 rising totals (25 for
    // level 2, 80 for level 3, ...). Identified by its shape, not yet
    // confirmed in the executable. Empty when the table is missing.
    static LevelCurve fromTables(const GameTables& tables);
    // Our own curve, for the open content: 30 x level^2 in total.
    static LevelCurve standard();

    int maxLevel() const { return static_cast<int>(thresholds.size()) + 1; }
    int levelFor(std::int64_t experience) const;
    // Total experience at which `level` begins (0 for level 1).
    std::int64_t start(int level) const;
};

// What the hero gains from levels alone, before equipment: our own
// numbers until the original's are found.
struct LevelStats {
    int maxHp = 0;
    int attack = 0;   // added to the weapon's attack
    int defense = 0;  // added to the armour's defense
};
LevelStats statsAtLevel(int level);

// Experience for defeating a monster whose placement gives none (the open
// content's enemy lines may leave it out).
int defaultExperience(int hp, int attack);

}  // namespace sf
