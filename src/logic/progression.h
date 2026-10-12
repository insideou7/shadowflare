#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
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
        kAttackSpeed, kWalkSpeed, kHp, kMp, kStrength, kAttack, kDefense,
        kMagicAttack, kMagicDefense, kHit, kEvasion, kMagicHit, kMagicEvasion,
        kCount
    };
    std::array<int, kCount> value{};
    int operator[](Stat s) const { return value[s]; }
};

// The original's classes ("professions" in its manual), numbered as the
// scenario scripts' ENABLECLASSCHANGE and GETCLASS number them.
// ShadowFlare.exe keeps them as job numbers: 16 mercenary, 6 warrior,
// 5 hunter, 9 wizard (the witch, for a heroine).
enum class HeroClass { Mercenary, Warrior, Hunter, Wizard };
inline constexpr int kHeroClassCount = 4;
std::string_view className(HeroClass cls);

// Stats from the hero's class history, as the original's level-up function
// builds them (ShadowFlare.exe 0x412fb0). `history` holds the class the
// hero had at each level-up, oldest first, so its size is the level minus
// one. They start with the male hero's starting stats (table 900, column
// 0); each level-up then adds the next column of that class's table: the
// n-th level gained in a class reads column n - 1 (warrior 902, hunter 906,
// wizard 904), except that a new hero already counts as one mercenary level,
// so mercenary level-ups read table 900 from column 1.
HeroStats heroStats(const std::vector<HeroClass>& history);

// A hero who was a mercenary up to level 5 and `cls` after it, as the
// original makes everyone a warrior at level 5 (see Career).
std::vector<HeroClass> classHistory(int level, HeroClass cls);
inline HeroStats heroStats(int level, HeroClass cls = HeroClass::Warrior) { return heroStats(classHistory(level, cls)); }

// The hero's class and how it changes, as in the original. Every hero
// starts as a mercenary and becomes a warrior on reaching level 5
// (0x413424). From then on each level-up looks at how the monsters were
// killed during the level just finished (0x413379): at least 20% with magic
// offers the wizard, otherwise at least 30% with a bow the hunter, otherwise
// at least 50% with swords, axes and maces the warrior; with none of those
// the offer goes back to the class the hero has. A guild instructor's
// script can also offer a class (ENABLECLASSCHANGE, 0x4341da). The hero
// takes an offered class with the Change Career button in the status window
// (0x4474b0); stats already gained stay, and later levels grow as the new
// class.
struct Career {
    HeroClass current = HeroClass::Mercenary;
    HeroClass offered = HeroClass::Mercenary;
    std::vector<HeroClass> history;  // the class at each level-up, oldest first
    // Kills during this level by the player's own blow: all of them, and by
    // weapon type (Item.Ibn weapon subtypes 0-7; 8 counts staffs, rods and
    // magic).
    int kills = 0;
    std::array<int, 9> killsBy{};

    static constexpr int kMagic = 8;

    // A hero at `level` as classHistory makes them.
    static Career at(int level, HeroClass cls = HeroClass::Warrior);
    // Counts a monster the player killed (0x4134a0). `weaponSubtype` is the
    // subtype of the weapon in hand, if any; `magic` that a spell did it.
    void countKill(std::optional<int> weaponSubtype, bool magic);
    // The hero has just reached `level`: records the class of the level left
    // behind, works out the offer and starts counting kills again.
    void levelUp(int level);
    bool canChange() const { return offered != current; }
    // Takes the offered class (the Change Career button). False if there is
    // none.
    bool change();
};

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
