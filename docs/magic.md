# Elements, spells and skills

How ShadowFlare.exe handles elements, magic points and the 22 spells, and
what the engine does with them. Addresses are in ShadowFlare.exe. The
numbers live in `System/Game/Parameter/Table.Tbd`; the open content uses
the same numbers (`MagicTables::standard()` in `src/logic/magic_tables.cpp`
matches the original's tables exactly), so it plays the same.

## Elements

Eight elements, numbered as in Table.Tbd table 19, items and monsters:
0 fire, 1 water, 2 earth, 3 thunder, 4 holy, 5 dark, 6 gel, 7 metal. Each
pair is opposites (fire and water, earth and thunder, holy and dark, gel
and metal): the opposite of `e` is `e ^ 1` (0x412a40 works it out as
`e / 2 * 2 - e % 2 + 1`).

The hero has a level in each, from -10 to 10. Their own levels come from a
point on the attribute disc (player + 0x1d4): each element has an anchor
on a circle of radius 20000 and the level is (20000 - distance) / 1000
(0x44fba0). A new hero stands in the middle, so all start at 0. Worn
weapons and armour add their element array (item + 0xc8, eight i32), and
the sum is kept in -10..10 (0x44fca0, 0x44fe30).

At the end of 0x44ea60 each level raises one stat by that many percent:
fire attack, water defence, earth hit, thunder evasion, holy magic attack,
dark magic defence, gel magic hit, metal magic evasion. A pair both at 10
also adds 10%: fire and water to maximum HP, earth and thunder to attack
speed, holy and dark to maximum MP, gel and metal to chant speed.

## Damage (0x412a40)

`r` is a random 0-2, so blows do 90, 100 or 110 percent; `r4` is 0-3.

- The hero's blow on a monster: attack x (9 + r) x T7 / 100 - defence,
  where T7 is table 7 at the hero's level in the monster's *opposite*
  element + 10 (8 at -10 up to 12 at 10).
- A monster's blow on the hero: (9 + r) x power / 10 - defence x T11 / 10,
  T11 being table 11 at the hero's level in the attack's element. A
  monster's spell is the same with magic defence.
- The hero's spell on a monster: (9 + r) x power x m / 100 - magic
  defence, with m = 10 + r4 when the spell's element is the monster's
  opposite, 7 + r4 when it is the same, else 10.

Every hit does at least 1. The chance to hit is hit minus evasion, kept to
20-98 percent (0x413e00); spells use magic hit against magic evasion
(0x45eb2b). The monster's words for all of this are in
[formats.md](formats.md#scenarios-scenarioidscenariomct).

## Spells

Table.Tbd, one row per spell (0-21) and, where it varies, a column per
spell level (1-30), read through 0x417410:

| table | what |
|---|---|
| 16 | MP cost |
| 17 | power: damage, the percent Heal restores, Energy Shield's defence percent, Magic Shield's percent taken off, Counter Burst's percent sent back |
| 18 | bonus to hit |
| 19 | element (column 0) |
| 20 | casting speed, per mille (column 0) |
| 21 | a flag, use not known yet |
| 27 | uses to go up from each level |
| 35 | missile speed; 0 for spells without a missile |
| 4 | chant speed / 32 to a step of the casting speed scale |
| 100-103 | class point weights (mercenary, warrior, wizard, hunter) |
| 104-107 | the class points at which each class learns each spell (99999 = never) |
| 201 | Berserker: MP drain per 10000 of the maximum (row 0) and its percent changes to 12 stats (rows 1-12) |
| 600-621 | each spell's name and description |

A spell's power is table 17 plus the hero's magic attack (at least 1),
its hit table 18 plus magic hit (Fire Ball's cast, 0x4399db). Casting a
spell costs table 16, less what equipment saves, at least 1 (0x44f610).
Spells are trained to level 20 (0x44f6f0); equipment can add levels up to
30 when cast (0x451e60).

### Learning (0x412fb0, 0x41334f)

There are no spell books or trainers. At each level-up the hero counts
their class points: every level spent in each class, counting the new
hero's first level as a mercenary one, times tables 100-103. A spell is
learned, at level 1, once the points of the class just finished reach that
class's threshold in tables 104-107, and goes in the first free place on
the F1-F8 bar. A mercenary learns Transport at level 2 and Plasma at
level 5; a warrior Fire Ball after 8 warrior levels.

### Training

Spells go up by use (table 27 says how many): attack spells for each
monster they hit, Heal for each cast, Moon, Berserker and Energy Shield
for each kill while they are on (0x419c91), Magic Shield and Counter Burst
each time they soak a blow of 20 or more.

### Shields and Berserker (0x443cb0)

- Energy Shield: against blows, the hero's defence is multiplied by table
  17 percent, and while there is MP the damage comes out of MP instead of
  HP.
- Magic Shield: against spells, damage x (100 - table 17) / 100, at least 1.
- Counter Burst (0x444150): after a monster's blow, it takes damage x
  table 17 / 100 back (half against monsters whose word 7 is 100).
- Each time Magic Shield or Counter Burst works it costs MP; at 0 MP it
  goes off. **The original charges the cost of the spell chosen on the bar
  at the shield's level, not the shield's own cost.** That is a bug in the
  original; we charge the shield's own cost unless original mode is on.
- Berserker changes the stats by table 201 and drains MP every third
  frame (0x44f2f0: maximum x rate / 10000, the fraction carried over); it
  goes off at 0 MP.

There is no MP or HP recovery over time of the hero's own; only
equipment gives it.

## In the engine

`src/logic/magic.h` has the rules and tables, `src/game/spells.cpp` the
casting. F1-F8 choose a spell from the bar and the right button casts it
at the cursor; casting a shield or Berserker again turns it off. The
status window shows MP and the magic stats, and the HUD an MP bar under
the health bar. Ours rather than the original's, until they are read from
the exe:

- Missiles fly table 35's speed x 1.5 world units a tick, up to 2400
  units, and hit the first monster within 110 units. Spells without a
  missile hit every monster within 320 units of the point cast at.
- The effects are drawn from simple shapes in each element's colour.
- Transport, Moon, Medusa, Identify and Explosion are not in yet.
- Monster spells land halfway through their animation, like blows.
- Chant speed and equipment that adds spell levels or saves MP are not
  read from items yet.

## Original mode

The menu's Original Mode (and `--original`) plays by the original's rules
with none of our fixes; it is off by default. Each fix is a switch in
`src/logic/rules.h`, and original mode turns them all off:

| switch | ours | original |
|---|---|---|
| `shieldsCostTheirOwnMp` | Magic Shield and Counter Burst cost their own MP | they cost the chosen spell's MP |

Data the engine used to misread, such as the monster stat words, is
corrected in both modes.
