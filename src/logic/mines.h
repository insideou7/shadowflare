#pragma once

#include <array>

#include "logic/magic.h"

namespace sf {

// Land mines, as ShadowFlare.exe lays and sets them off (B or the MINE
// button, 0x442a73 and 0x445fc5; the mine's controller is 0x42bd40). Times
// are the original's ticks, 30 a second. See docs/mines.md.
namespace mines {

// A new hero carries five mines and can carry ten (0x4414db, 0x4412c6);
// worn items with the Mine Count modifier raise the most (0x44ea60).
inline constexpr int kStartCount = 5;
inline constexpr int kBaseMost = 10;
// After laying one, the B key and the button wait this long (0x48d5d8).
inline constexpr int kLayWait = 10;
// The mine lies for 300 ticks, then goes off by itself. Monsters set it
// off from tick 45 on, and its picture shows it armed from tick 40.
inline constexpr int kLife = 300;
inline constexpr int kArmedAt = 45;
inline constexpr int kArmedPictureAt = 40;
// A monster sets it off by coming within 150 of it; the blast hurts every
// monster within 600, once, with a hit of 9999 (always 98% after evasion).
inline constexpr int kTriggerHalf = 150;
inline constexpr int kBlastHalf = 600;
inline constexpr int kBlastHit = 9999;
// After the blast: smoke rings every other tick from tick 12 to 40, each
// 50 further out, and four pieces of the mine thrown up at tick 12 that
// fly 20 a tick for 60 ticks and fall back (velocity 1500 down 100 a tick,
// height += velocity / 10, bouncing at 400). The controller ends at 80.
inline constexpr int kRingsFrom = 12, kRingsTo = 40, kRingStep = 50;
inline constexpr int kPiecesAt = 12, kPieceLife = 60, kPieceSpeed = 20;
inline constexpr int kPieceLift = 1500, kPieceFall = 100, kPieceBounce = 400;
inline constexpr int kDone = 80;

// Character/OPTION effects: the mine on the ground (a Pattern bank, picture
// 0 laid and 1 armed), the blast, two kinds of smoke, the fire that rides
// on each piece, and the four pieces.
inline constexpr int kMineOption = 1000, kBlastOption = 1001, kSmokeOptions[2] = {1002, 1003}, kPieceFireOption = 1004,
                     kPieceOptions[4] = {1005, 1006, 1007, 1008};
// What a monster hit by the blast shows: one of four, at random (the
// controllers 21000-21003 play these).
inline constexpr std::array<int, 4> kHitOptions = {11000000, 11000001, 11000002, 11000009};

// The most mines the hero can carry with `bonus` from worn items.
int most(int bonus);
// The blast's power: table 23 at the hero's level plus the Mine Effect
// modifiers worn, at least 1 (0x42bd40).
int power(const MagicTables& tables, int heroLevel, int bonus);
// What the blast does to a monster: a blow of that power with no element
// levels behind it, against the monster's defence (0x412a40).
int damage(const MagicTables& tables, int power, int monsterElement, int defense, int roll);

}  // namespace mines
}  // namespace sf
