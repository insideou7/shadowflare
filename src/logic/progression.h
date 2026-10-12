#pragma once

#include <cstdint>
#include <vector>

namespace sf {

struct GameTables;

// How much experience the player needs for each level. As in the
// original, experience counts up from 0 within a level: needs[i] is what
// level i + 1 takes to reach level i + 2, and levelling up starts the
// count again from 0 (anything over the need is lost).
struct LevelCurve {
    std::vector<std::int64_t> needs;

    // Table.Tbd table 13 of the original: 100 rows of experience per level
    // (25 for level 1, 80 for level 2, ...), closed by 999999999 and 0.
    // The executable compares the player's experience with row level - 1
    // and resets it to 0 on a level up. Empty when the table is missing.
    static LevelCurve fromTables(const GameTables& tables);
    // The same numbers, for the open content: it plays like the original.
    static LevelCurve standard();

    int maxLevel() const { return static_cast<int>(needs.size()) + 1; }
    // Experience `level` needs to reach the next one; 0 at the top.
    std::int64_t need(int level) const;
};

// What the hero gains from levels alone, before equipment.
struct LevelStats {
    int maxHp = 0;
    int attack = 0;   // added to the weapon's attack
    int defense = 0;  // added to the armour's defense
};
// Health follows the original (Table.Tbd tables 900 and 902): 150 at
// level 1, 40 more for each level to 5, then a warrior's 50 for 20 levels,
// 40 for 10 and 30 after that. Attack and defence are still our own.
LevelStats statsAtLevel(int level);

// Experience for defeating a monster whose placement gives none (the open
// content's enemy lines may leave it out).
int defaultExperience(int hp, int attack);

}  // namespace sf
