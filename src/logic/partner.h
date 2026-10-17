#pragma once

#include <array>
#include <optional>
#include <string>

#include "formats/table_data.h"

namespace sf {

class Inventory;
struct MagicTables;

// The hero's animal companion, as ShadowFlare.exe runs it (the partner
// object, ctor 0x45f410). Pure rules; the game moves and draws it.
inline constexpr int kPartnerKinds = 6;  // rows of Table.Tbd table 60
inline constexpr int kPartnerMaxLevel = 35;

// One kind of partner: Table.Tbd table 60 (0 Kerberos, 1 Gravity, 2 Dune,
// 3 Fang, 4 Harley, 5 Hawk).
struct PartnerKind {
    std::string name;
    int folder = 0;  // Character/PARTNER/<folder as %08d>; the four dogs share 0
    std::array<int, 3> tint{1000, 1000, 1000};  // red, green, blue in thousandths
};

// A partner's stats at a level (0x4136f0): Table.Tbd table 800 + kind, one
// column per level. Rows 0-12 add up the columns up to the level (column 0
// is the base, the rest what each level adds); rows 13-17 are the value at
// the level.
struct PartnerStats {
    int attackSpeed = 128;
    int walkSpeed = 125, runSpeed = 225;  // the hero's walking speed is 128
    int maxHp = 1;
    int attack = 0, hit = 0, defense = 0, evasion = 0;
    int magicAttack = 0, magicDefense = 0, magicHit = 0, magicEvasion = 0;
    int element = -1;  // 0 fire ... 7 metal
    // The jump (rows 14-17): how likely its landing stuns a monster, in
    // percent, for how many ticks, whether that may run past 15 ticks, and
    // the side of the square it lands on.
    int jumpStunChance = 0, jumpStunLength = 0;
    bool jumpStunUncapped = false;
    int jumpArea = 0;
};

// What the hero's worn items add (modifiers 30-38, 0x4138b0): health,
// attack, hit, defence, evasion and the four magic stats, each left at
// least 1.
void addItemBonuses(PartnerStats& stats, const Inventory& worn);
// The hero's Moon at `level` (0x4138b0): table 200 rows 1-13 raise each of
// the companion's stats, in the order of its own table's rows 0-12, by
// that many percent; then the speeds are kept to 0-255 and the rest at
// least 1.
void applyMoon(PartnerStats& stats, const MagicTables& tables, int level);

class PartnerTables {
public:
    // Reads tables 60 and 800-805 (Table.Tbd's, or an open pack's
    // partners.txt); nothing when they are missing. Keeps its own copy.
    static std::optional<PartnerTables> load(const GameTables& tables);

    const PartnerKind& kind(int kind) const { return kinds_[static_cast<std::size_t>(kind)]; }
    PartnerStats stats(int kind, int level) const;
    // Experience a partner needs at `level` to reach the next (row 18).
    int need(int kind, int level) const;

private:
    GameTables tables_;  // just tables 800-805
    std::array<PartnerKind, kPartnerKinds> kinds_;
};

// What the hero keeps about their partners (player +0x150-0x15c, +0x1590,
// +0x1594, +0x15a0): which one is with them, each kind's level and
// experience, whether it fights, and how long until it comes back.
struct PartnerRecord {
    int kind = 0;  // a new hero has Kerberos
    std::array<int, kPartnerKinds> levels{1, 1, 1, 1, 1, 1};
    std::array<int, kPartnerKinds> experience{};
    // ACTIVE partners seek out monsters and can be hurt; INACTIVE ones
    // follow, can't be hurt and heal. New heroes start INACTIVE (0x4414f1).
    bool active = false;
    int reviveTicks = 0;  // counting down while the partner is dead

    int level() const { return levels[static_cast<std::size_t>(kind)]; }

    // A monster killed by the hero or the partner, while the partner lives:
    // one experience point, levelling up as the table says, never past
    // levelCap (0x413640, 0x412e20). True when it gained a level. amount is
    // ours (the Experience Rate cheat): more points per kill, carried across
    // levels up to the cap.
    bool countKill(const PartnerTables& tables, int heroLevel, int amount = 1);
};

// How high a partner can go: min(35, hero level / 3 + 2) (0x41362b).
int partnerLevelCap(int heroLevel);

// Ticks a dead partner takes to come back: 900, or 600 when the hero
// carries a Companion Stone (0x461a34).
inline constexpr int kPartnerReviveTicks = 900, kPartnerReviveTicksWithStone = 600;

}  // namespace sf
