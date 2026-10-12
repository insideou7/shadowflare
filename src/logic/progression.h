#pragma once

#include <array>
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

// The hero's own stats before equipment, as the original keeps them: 13
// numbers, in the row order of Table.Tbd tables 900-907.
struct HeroStats {
    enum Stat {
        kAttackSpeed, kWalkSpeed, kHp, kMp, kSp, kAttack, kDefense,
        kMagicAttack, kMagicDefense, kHit, kEvasion, kMagicHit, kMagicEvasion,
        kCount
    };
    std::array<int, kCount> value{};
    int operator[](Stat s) const { return value[s]; }
};

// The original's classes, in the order GETCLASS reports them. A new hero
// has none until a guild's script gives them one with ENABLECLASSCHANGE
// (ShadowFlare.exe 0x4341da). Content without scripts makes the hero a
// warrior from level 6 (classAtLevel).
enum class HeroClass { Novice, Warrior, Magician, Hunter };
HeroClass classAtLevel(int level);

// Stats at a level, as the original's level-up function builds them
// (ShadowFlare.exe 0x412fb0): the male hero's starting stats (table 900,
// column 0), what each classless level adds (columns 1-4), then what each
// level in the class adds (warrior 902, magician 904, hunter 906; the n-th
// level in a class reads column n - 1). `classFrom` is the level the class
// was taken at: levels up to it are classless (at most four of them add
// anything), levels after it are the class's.
HeroStats heroStats(int level, HeroClass cls = HeroClass::Warrior, int classFrom = 5);

// Combat, as the original resolves a blow between the player and a monster
// with no element involved. The chance to hit is hit minus evasion, kept
// between 20 and 98 percent (0x413e00). Damage is the attack times 90, 100
// or 110 percent (`roll` 0, 1 or 2), minus the defence, and at least 1
// (0x412a40).
int hitChance(int hit, int evasion);
int meleeDamage(int attack, int defense, int roll);

// Experience for defeating a monster whose placement gives none (the open
// content's enemy lines may leave it out).
int defaultExperience(int hp, int attack);

}  // namespace sf
