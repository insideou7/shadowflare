#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "logic/progression.h"

namespace sf {

struct GameTables;

// The eight elements, numbered as ShadowFlare.exe numbers them (Table.Tbd
// table 19, an item's element array, a monster's Mct word 6). Each one's
// opposite is its neighbour in the pair: fire and water, earth and thunder,
// holy and dark, gel and metal.
enum Element { kFire, kWater, kEarth, kThunder, kHoly, kDark, kGel, kMetal };
inline constexpr int kElementCount = 8;
std::string_view elementName(int element);
constexpr int oppositeElement(int element) { return element ^ 1; }

// The hero's level in each element, -10 to 10.
using ElementLevels = std::array<int, kElementCount>;

// The hero's own element levels come from a point on the attribute disc
// (player + 0x1d4): each element has an anchor on a circle of radius 20000
// and its level is (20000 - distance to the anchor) / 1000 (0x44fba0). A new
// hero stands in the middle, so every level starts at 0. What the hero
// wears is added to that, and each level is kept between -10 and 10
// (0x44fca0, 0x44fe30).
ElementLevels discElementLevels(int x, int y);
ElementLevels elementLevels(const ElementLevels& base, const ElementLevels& worn);

// The 22 spells, numbered as the original numbers them (Table.Tbd tables
// 600-621 hold their names and descriptions).
enum Spell {
    kTransport, kFireBall, kIceBolt, kPlasma, kHellFire, kIceBlast, kHeal, kMoon, kBerserker, kEnergyShield,
    kEarthSpears, kFlameStrike, kDreadDeathscythe, kLightningStorm, kMedusa, kSonicBlade, kMudJavelin, kIdentify,
    kMagicShield, kCounterBurst, kExplosion, kElementalStrike
};
inline constexpr int kSpellCount = 22;
inline constexpr int kSpellColumns = 30;   // tables 16-21 and 35 have a column per level up to 30
inline constexpr int kMaxSpellLevel = 20;  // what spells can be trained to (0x44f6f0)
inline constexpr int kSpellBarSize = 8;    // F1-F8
inline constexpr int kNever = 99999;       // tables 104-107: never learned in that class

// Everything about magic that the original keeps in Table.Tbd.
struct MagicTables {
    struct SpellInfo {
        std::string name;
        std::vector<std::string> description;
        int element = 0;       // table 19
        int castSpeed = 1000;  // table 20, per mille of the casting animation's speed
        // By level - 1: MP cost (16), power (17), bonus to hit (18), the
        // speed of its missile, 0 for spells with none (35), and table 21,
        // a flag whose use is not known yet.
        std::array<int, kSpellColumns> mpCost{}, power{}, hit{}, speed{}, flag21{};
        // Table 27: uses to go from level n + 1 to n + 2.
        std::array<int, kMaxSpellLevel> usesToLevel{};
        // Tables 104-107: the class points at which a hero of each class
        // learns it, in table order (mercenary, warrior, wizard, hunter).
        std::array<int, 4> learnAt{kNever, kNever, kNever, kNever};
    };
    std::array<SpellInfo, kSpellCount> spells;
    // Tables 7 and 11 by element level + 10: how a hero's level in the
    // monster's opposite element strengthens their blows (7), and how their
    // level in the attacker's element strengthens their defence (11), in
    // tenths.
    std::array<int, 21> elementAttack{}, elementDefense{};
    // Tables 100-103: points towards each class's spells per level spent in
    // each class, both in table order (mercenary, warrior, wizard, hunter).
    std::array<std::array<int, 4>, 4> classWeights{};
    // Table 4: chant speed / 32 -> step on the casting speed scale.
    std::array<int, 8> chantSteps{};
    // Table 201, by level - 1: Berserker's MP drain per 10000 of the maximum
    // (row 0) and its percent changes to the 12 stats (rows 1-12: attack
    // speed, walking speed, HP, MP, attack, defence, magic attack, magic
    // defence, hit, evasion, magic hit, magic evasion).
    std::array<std::array<int, kSpellColumns>, 13> berserker{};
    // Tables 204-207, by level - 1: how many of something a spell makes.
    // 204: Flame Strike's fireballs, Dread Deathscythe's blades and
    // Lightning Storm's rays; 205: Plasma's bursts; 206: Earth Spears'
    // spears; 207: Elemental Strike's orbs.
    std::array<std::array<int, kSpellColumns>, 4> counts{};
    int count(int table, int level) const { return counts[static_cast<std::size_t>(table - 204)][column(level)]; }
    // Table 23, by hero level - 1: a mine's power (logic/mines.h).
    std::array<int, 100> minePower{};

    // The original's numbers, for the open content: it plays like the
    // original.
    static MagicTables standard();
    // From the player's Table.Tbd; tables it lacks keep the standard numbers.
    static MagicTables fromTables(const GameTables& tables);

    const SpellInfo& spell(int s) const { return spells[static_cast<std::size_t>(s)]; }
    // Table 16-21/35 lookups at a spell level (1-30), as 0x417410 reads them.
    int mpCost(int s, int level) const { return spell(s).mpCost[column(level)]; }
    int power(int s, int level) const { return spell(s).power[column(level)]; }
    int hit(int s, int level) const { return spell(s).hit[column(level)]; }
    int speed(int s, int level) const { return spell(s).speed[column(level)]; }
    static std::size_t column(int level) { return static_cast<std::size_t>(std::clamp(level, 1, kSpellColumns) - 1); }
};

// Spells that stay on until cast again (or MP runs out) rather than act once.
bool isToggleSpell(int spell);
// Spells trained by kills while they are on rather than by casting (0x419c91).
bool trainsByKills(int spell);

// The hero's stats after element levels, as the end of 0x44ea60 works them
// out from the stats with equipment added: each element raises one stat by
// its level in percent (fire attack, water defence, earth hit, thunder
// evasion, holy magic attack, dark magic defence, gel magic hit, metal magic
// evasion); a pair both at 10 adds 10% more to maximum HP (fire and water),
// attack speed (earth and thunder), maximum MP (holy and dark) or chant
// speed (gel and metal). Then the stats are kept in range.
void applyElementLevels(HeroStats& stats, int& chantSpeed, const ElementLevels& levels);
// Berserker at `level` changes the stats by table 201's percentages.
void applyBerserker(HeroStats& stats, int& chantSpeed, const MagicTables& tables, int level);
// Attack speed, walking speed and chant speed 0-255, strength at least 0,
// everything else at least 1.
void clampStats(HeroStats& stats, int& chantSpeed);

// Damage, as 0x412a40 works it out. `roll` is 0-2 (90-110%); `roll4` 0-3.
// A monster element of -1 (open content) counts as no element.
//
// The hero's blow: attack x 90-110% x table 7 at the hero's level in the
// monster's opposite element, minus the monster's defence.
int heroBlowDamage(const MagicTables& t, int attack, const ElementLevels& levels, int monsterElement, int defense, int roll);
// A monster's blow or spell on the hero: power x 90-110% minus the hero's
// defence (magic defence for spells) x table 11 at their level in the
// attack's element.
int damageToHero(const MagicTables& t, int power, int element, const ElementLevels& levels, int defense, int roll);
// The hero's spell on a monster: power x 90-110%, times 100-130% when the
// spell's element is the monster's opposite or 70-100% when it is the same,
// minus the monster's magic defence.
int spellDamage(int power, int spellElement, int monsterElement, int magicDefense, int roll, int roll4);

// The original's class points (0x412fb0): every level the hero has spent in
// a class, counting the new hero's first level as a mercenary one, weighted
// by tables 100-103. Indexed in table order: mercenary, warrior, wizard,
// hunter.
std::array<int, 4> classPoints(const MagicTables& t, const std::vector<HeroClass>& history);
int tableClassIndex(HeroClass cls);

// What the hero knows of magic.
struct SpellBook {
    std::array<int, kSpellCount> level{};  // 0 = not learned (player + 0x1498)
    std::array<int, kSpellCount> uses{};   // towards the next level
    std::array<int, kSpellBarSize> bar{-1, -1, -1, -1, -1, -1, -1, -1};
    int selected = -1;  // the spell the right button casts, -1 = none

    bool knows(int spell) const { return spell >= 0 && spell < kSpellCount && level[static_cast<std::size_t>(spell)] > 0; }

    // Called on each level-up after Career::levelUp has recorded the class
    // just finished: learns, at level 1, every spell whose threshold for
    // that class its class points now reach, and puts it in the first free
    // place on the bar (0x41334f). Returns the spells learned.
    std::vector<int> learn(const MagicTables& t, const std::vector<HeroClass>& history);
    // A hero who has reached their level with this history, as if every
    // level-up had happened (for saves from before magic).
    static SpellBook replay(const MagicTables& t, const std::vector<HeroClass>& history);

    // One use towards the spell's next level (0x44f6f0). True when it went up.
    bool train(const MagicTables& t, int spell);
    // The level a spell is cast at (0x451e60): its own, +2 in power mode,
    // at most 20, plus what equipment adds, at most 30.
    int castLevel(int spell, bool powerMode = false, int equipmentBonus = 0) const;
};

// What a spell costs (0x44f610): table 16 less what equipment saves, at
// least 1.
int spellMpCost(const MagicTables& t, int spell, int level, int equipmentSaving = 0);

// The casting animation's speed factor (0x441208 and the cast code): table
// 20 per mille times a step chosen by chant speed through table 4 (step 9
// in power mode, 0 when carrying too much).
double castSpeedFactor(const MagicTables& t, int spell, int chantSpeed, bool powerMode = false, bool overloaded = false);

// HP or MP recovered every third frame (0x44f2f0): maximum x rate / 10000,
// with the fraction carried over in `carry` (in hundredths of a point).
int regenerate(int maximum, int rate, int& carry);

}  // namespace sf
