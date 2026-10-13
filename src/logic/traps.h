#pragma once

#include <optional>

#include "formats/map_layers.h"

namespace sf {

// Traps and map effects that scenario scripts set off (ShadowFlare.exe;
// see docs/traps.md). No SDL, so they can be unit tested.

// A judge rectangle around a position, in world units.
struct JudgeRect {
    int left = 0, top = 0, right = 0, bottom = 0;
};

// The hero's judge rectangle (player + 0x2e8, set at 0x44a4c0).
inline constexpr JudgeRect kHeroJudge{-80, -80, 79, 79};

// CALCANGLE dx dy var: the angle towards (dx, dy) in whole degrees,
// counter-clockwise from world x with world y turned up, cut towards zero
// as 0x432831 does: atan2(-dy, dx) * 57.2957914...
int calcAngleDegrees(int dx, int dy);

// The direction 0-7 an angle in radians (world y up) faces, as 0x4140f0
// works it out: 45-degree slices, 1 centred on world x, then on round
// clockwise on the screen to 7, and 0 between 7 and 1.
int directionOfAngle(double radians);

// The gap between two judge rectangles (0x4143c0), which GETNEARPLAYERPOS
// and CALCMYPLAYERDIST measure: 0 when they overlap, one less than the
// gap when they are side by side, and one less than the corners' distance
// (cut to a whole number) when they are apart both ways.
int rectGap(int ax, int ay, const JudgeRect& a, int bx, int by, const JudgeRect& b);

// What a CREATEOPTIONANGLE kind fires (0x42a300). `direction` is the
// script's (-1 or 8 for none) and `angle` the shot's, in radians, world y
// up.
struct TrapShot {
    int option = 0;     // Character/OPTION folder
    int direction = 8;  // drawn facing 0-7, 8 for no direction
    JudgeRect box;      // where it hurts, around where it is
    int hitFrom = 0;    // the ticks of its life it hurts in
    int hitTo = -1;     // -1: to the end
    // How long it lasts: until it hits something or meets a wall, or the
    // length of its animation in its direction (the fire jets).
    bool lastsItsAnimation = false;
    bool hitsAll = false;     // everyone in the box (each tick), else the nearest
    bool diesOnHit = false;
    bool diesOnWall = false;
    // The Voice00.Voc sound of a hit that lands (the create struct's
    // words 0x76 and 0x77), -1 for none.
    int hitSound = -1;
};
// Empty for the kinds no script uses (4-7), which the exe sets up further.
std::optional<TrapShot> trapShot(int kind, int direction, double angle);

// The effect a script's effect number plays (CREATEEFFECT and
// CREATEEFFECTCHAR, through the controllers 0x429ec0 hands to 0x42b860):
// a Character/OPTION folder, or 0 for a number that is not a plain effect.
int scriptEffectOption(int controller);

// The sparks a trap's hit shows on the hero (0x43309b picks the controller
// and 0x443cb0 plays it): one of 11000020-22 for an arrow (kind 0), else
// one of 11000000, 01, 02 and 09. `roll` is the random pick, 0-3.
int trapHitOption(int kind, int roll);

// Whether a shot's box at (x, y) meets a wall (0x4145b0 as 0x45e1e0 calls
// it): a judge cell under any part of the box that is blocked (bit 0) and
// is not one shots fly over (bit 1). Cells off the grid do not count.
bool shotMeetsWall(const GroundLayer& ground, int x, int y, const JudgeRect& box);

// How a blow stuns the hero or a companion (0x443cb0, 0x45f9f0): with
// `chance` less `chanceResist` percent (`roll` is 0-99) it holds them
// `length` less `lengthResist` original ticks, at least 1 and, unless the
// blow is `steady`, at most 15. Empty when it does not stun.
std::optional<int> stunTicks(int chance, int length, bool steady, int chanceResist, int lengthResist, int roll);

// How far a stunned hero or companion is pushed on tick `tick` of a stun
// `total` ticks long (the hit state, 0x435870): (total - tick) x 120
// / total world units, straight away from the blow.
int stunPush(int tick, int total);

}  // namespace sf
