#pragma once

#include <vector>

#include "logic/progression.h"

namespace sf {

// Bowguns and slings (Item.Ibn weapon subtype 5), as ShadowFlare.exe shoots
// them (see docs/archery.md). No SDL, so they can be unit tested.

// What a bowgun shoots (weapon word 0xfc).
enum class ShotKind { Stone = 0, Arrow = 1, FireArrow = 2, IceArrow = 3 };

// How many of the hero's level-ups were taken as a hunter (0x450f80 counts
// the class bytes at player + 0xec; the first is the new hero's mercenary).
int hunterLevels(const std::vector<HeroClass>& history);

// The percent of the hero's attack a bowgun shot carries (0x437fe0): all of
// it for a hunter; anyone else gets 40% plus hunter levels x 50 / 30
// (cut to a whole number), at most 90%. The status window shows the
// attack scaled the same way (0x405750).
int bowAttackPercent(HeroClass current, const std::vector<HeroClass>& history);

// One shot of a volley: where it flies (radians, world y up, as the
// original measures angles) and the direction 0-8 it is drawn facing.
struct BowArrow {
    double angle = 0;
    int facing = 8;
};
// What one pull of a bowgun's trigger looses (0x437fe0 and the shot
// controllers it hands them to, 0x429ec0 then 0x42a300).
struct BowVolley {
    int option = 0;        // Character/OPTION folder: 0 the arrow, 1 the stone
    bool flame = false;    // the arrow's flame layer (2) is shown: fire bowguns
    bool frost = false;    // its frost layer (3): ice bowguns
    std::vector<BowArrow> arrows;
};
// `kind` is weapon word 0xfc and `spread` word 0x100: 0 one shot, 1 two
// 8 degrees either side, 3 three 15 degrees apart, 5 five 10 apart and 7
// seven 8 apart (2, 4, 6 and 8 are the same counts with each shot facing
// its own way; no item uses them). `angle` is towards the target, or the
// way the hero faces.
BowVolley bowVolley(int kind, int spread, double angle);

// The spark a shot shows on the monster it hits (the attack struct's
// effect, chosen once per pull: 21000-21003 for a stone, 21007-21009 for
// an arrow, as 0x42b860 plays them). `roll` is 0-3 for a stone, 0-2 for an
// arrow.
int bowHitOption(int kind, int roll);

// Increased-power mode's hunter attack (0x437fe0, 0x438a90): a hunter in
// power mode who starts a bowgun attack sets it off when rand() % 100 is
// under 33. It rains an arrow on every monster within 2000 of the hero
// (at most 100), each falling after a wait of rand() % 15 ticks.
inline constexpr int kHunterVolleyChance = 33;
inline constexpr int kHunterVolleyReach = 2000;
inline constexpr int kHunterVolleyMost = 100;
inline constexpr int kHunterVolleyWait = 15;
// The arrow that falls (missile 9000, Character/OPTION/00009000; 0x42b9e0):
// on tick `tick` (0-10) of its fall it is this far from its target and
// this high; it strikes at tick 10. The original's speed is 400.
struct FallingArrow {
    int dx = 0, dy = 0;  // from the target, world units
    int height = 0;
};
inline constexpr int kFallTicks = 10;
FallingArrow fallingArrow(int tick, int speed = 400);
// The spark it shows on what it hits (0x438a90's effect 20006).
inline constexpr int kHunterVolleyHitOption = 11000004;

}  // namespace sf
