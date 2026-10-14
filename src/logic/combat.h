#pragma once

namespace sf {

// The Warrior's combos and axe spin, as ShadowFlare.exe runs them (player
// states 11-18, 0x4364e0-0x437910). A Warrior (job 6) whose attack would be
// a sword's or a great sword's swing does a three-blow combo instead, and
// one with an axe or mace spins. See docs/combat.md.
enum class WarriorMove { None, Sword, GreatSword, Spin };

// The move a Warrior's attack becomes with a weapon of this Item.Ibn
// subtype in hand (0x450630 picks the attack state, 0x441c00 and 0x448740
// turn states 8, 9 and 10 into 11, 12 and 13): one-handed swords (0) the
// sword combo, two-handed swords (3) the great sword's, axes and maces (1)
// the spin. Anything else (no weapon, a staff, a bowgun) stays a plain
// attack.
WarriorMove warriorMove(int weaponSubtype);

// The hero's chart for a combo's blow 1-3: 5, 7 and 8 for the sword, 15,
// 17 and 18 for the great sword (0x4364e0, 0x436c20, 0x4372b0).
int comboChart(WarriorMove move, int blow);

// The voice the hero shouts as a combo's blow `blow` (1-3) falls: 0x60-0x62
// for the hero, 0x63-0x65 for the heroine. The first is the plain swing's.
int comboShout(int blow, bool heroine);

// The step (0-9) of the hero's speed scales their attack speed picks
// (0x450c60): table 4 at attack speed / 32, plus 1; 9 in power mode.
// `table4` is that table's value for the column.
int attackSpeedStep(int table4, bool powerMode);
// Frames a combo blow plays per original tick at a step (the scale each
// blow builds on the stack): 0.8 to 1.7.
double comboSpeed(int step);
// The same for the spin: 0.6 to 1.5, as the plain sword swing (0x435e60).
double spinSpeed(int step);

// How far a combo blow carries the hero towards where they aimed on
// original tick `n` of the blow (0, 1, 2...), until it lands: 0, 10, 20 ...
// 60 world units, then nothing (+0x1614, at most 60).
int comboLunge(int n);

// The spin (0x437910): chart 21 winds up facing where the hero aims, chart
// 22 (its ninth, direction-less slot, 8 frames) turns round three times,
// chart 23 comes out of it. `t` is the frame the spin has reached (original
// ticks x spinSpeed), `direction` the hero's facing and `windUp`, `turn`
// and `recover` the three charts' lengths.
struct SpinFrame {
    int chart = 21;
    int frame = 0;
    bool turning = false;  // in chart 22: the spin's blows and its march
    bool done = false;     // the last frame of chart 23
};
SpinFrame spinFrame(int t, int direction, int windUp, int turn, int recover);
// How far the spin carries the hero forward each original tick while it
// turns (0x437910): 30 world units.
inline constexpr int kSpinStride = 30;

// Which monsters a blow catches by direction (0x4415b0, 0x4417f0): the
// direction from the hero to the monster against the hero's own. A plain
// swing takes the one ahead or one step either side; a combo's blows two
// steps either side; the spin all round.
bool inSwing(int heroDirection, int toMonster, int spread);
inline constexpr int kSwingSpread = 1, kComboSpread = 2, kAllRound = 4;

}  // namespace sf
