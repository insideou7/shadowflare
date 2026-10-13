# Traps, effects, rain, story pictures and taught spells

These script commands come from ShadowFlare.exe. Their addresses are given
below, and the code is in `src/logic/traps.cpp`, `src/logic/script_runner.cpp`
and `src/game/traps.cpp`. None of them differs from the original, so original
mode has nothing to switch.

## Angles and distances

The original measures angles counter-clockwise from world x, with world y
pointing up. The engine's own angles have y pointing down, so a trap's angle
is negated when it is handed over.

- **Degrees to radians** uses the exe's constant 0.01745328888888889 (at
  0x4751b0). It is a shade under π/180, so a shot 100 units out at 90 degrees
  starts 99 units up, not 100.
- **`CALCANGLE dx dy var`** (0x432831) stores `(int)(atan2(-dy, dx) *
  57.2957914...)`, cut towards zero.
- **Direction from an angle** (0x4140f0) uses 45-degree slices. Direction 1 is
  centered on world x, and the slices go round to 7, with 0 between 7 and 1.
- **Judge rectangles.** The hero's is (-80, -80, 79, 79), set at 0x44a4c0.
  0x4143c0 measures the gap between two rectangles:
  - 0 when they overlap;
  - one less than the nearest pair of edges when they are side by side;
  - one less than the distance between the corners when they are apart both
    ways.
- **`GETNEARPLAYERPOS char min max player x y`** (0x43288d) finds the nearest
  living player whose gap from the character is between min and max. -1 means
  no limit. When it finds no one, `player` is -1 and x and y are left alone.
- **`CALCMYPLAYERDIST char var`** stores the gap between the hero and the
  character.
- **`GETMYPLAYERNO`** stores 0 in single play.

## `CREATEOPTIONANGLE`: trap shots

The operands are: x, y, kind, angle (degrees), speed, power, height, distance,
direction, fixed, stun chance, stun length, steady and splash. The last four
fill hit words 0x29, 0x2b, 0x28 and 0x48.

- The shot starts `distance` out along its angle, and each axis is cut to a
  whole number (0x4140a0).
- It goes to option controller `kind` (0x429ec0, then 0x42a300). It is drawn
  from `Character/OPTION/0000000k`.
- It hurts players, companions and blocking objects (mask 19).
- On each original tick (0x45e1e0), it hurts first, then checks for walls,
  then moves `speed`, then ages.
- **Walls.** Arrows (kinds 0, 1 and 3) stop at walls. If the shot's box
  already touches a blocked judge cell (bit 0), the shot is gone. The same
  happens if its path meets one while it moves, and it stops there (0x4145b0,
  0x414990). Cells with bit 1 set are low walls that shots fly over. The engine
  checks the path in steps of 40 units or less.
- **Objects.** A scenario object that stands in the way (its third state,
  which the exe keeps as status bit 0 at 0x45e080) stops an arrow the same
  way when its rectangle meets the shot's box. The exe also skips objects
  whose word at + 0xf8 is set; that word is not traced, and the engine
  takes it as clear.

| Kind | What it is | Box | Hurts | Lasts |
|---|---|---|---|---|
| 0 | arrow; faces its angle when the direction is -1 or 8 | ±30 | the nearest one in the box, then it is gone | until a hit or a wall |
| 1 | the same with no facing | ±30 | the same | the same |
| 2 | fire jet, along direction 1, 3, 5 or 7 | 360 long, 120 wide, out from the trap | everyone in the box, every tick from tick 5 to tick 30 | its animation's frames in that direction |
| 3 | arrow that always faces its angle | ±30 | the nearest one | until a hit or a wall |

**How a hit works**

- **Chance.** A hit must beat the target's evasion, using the monsters' hit
  chance.
- **Damage.** A `fixed` shot deals its power outright. Otherwise the power is
  a monster blow with no element, through 0x412a40, against the defense of
  whoever is hit. Power mode raises the hero's defense by a fifth.
- **Companions.** A companion is hit through its own code (0x45f9f0), with the
  same hit roll, damage rule and stun. Its gear does not resist the stun.
- **Sparks.** The hero shows sparks:
  - for an arrow (kind 0): one of 11000020-22;
  - for any other kind: one of 11000000, 01, 02 or 09.
- **Stun** (0x443cb0, 0x45f9f0). The chance is the stun chance less the
  hero's resistance. If the roll (0-99) is under it, the hero is held in the
  hit pose, its frames spread over the stun.
  - **Length.** The stun length less the hero's resistance, in original ticks.
    It is at least 1, and at most 15 unless the shot is steady.
  - **Table 25.** A chance or length of -1 comes from Table.Tbd table 25,
    from the row for how much of the hero's health the blow took
    (damage × 50 / max HP, at most row 49).
  - **Resistance.** The hero's resistance is modifiers 14 (chance) and
    15 (length) of the armor and accessories worn (0x450d90). The weapon does
    not count, and broken items do.
  - **Push back.** Unless the shot is steady, or modifier 16 is on something
    worn, the hero turns to face the blow and is pushed straight away from it
    (0x435870). On tick t of a stun T ticks long, the push is
    (T - t) × 120 / T units, as far as the ground allows.
  - **Not copied.** The original also reads some of the hero's own states,
    which can make a stun certain or impossible. The engine does not have
    those states.
- **Layers.** A trap's arrow (kind 0) is drawn plain: its flame (layer 2)
  and frost (layer 3) are hidden, as bowguns hide them for plain arrows
  (see [archery.md](archery.md)).
- **Sound.** An arrow plays voice 7 where it is when its blow lands. Fire
  jets have no sound. See [sound.md](sound.md).
- **Splash.** A shot with the splash word set shows 11000024 or 11000025 on
  one hit in five, facing a random direction.

Some scenarios point a trap's switch at a constant instead of a temp flag, so
those traps never fire. The original does the same, and it is kept.

## `CREATEEFFECT` and `CREATEEFFECTCHAR`

- **`CREATEEFFECT effect x y height dir`** plays an effect at a spot. A
  direction of -1 becomes 8, meaning no facing.
- **`CREATEEFFECTCHAR effect char`** plays an effect that stays on a
  character. Characters 0-3 are the players.

The effect numbers go through 0x42b860 to OPTION folders, as listed in
`scriptEffectOption`. For example, 21003 is 11000009 and 22000 is 12000000.

## `RESSURECT enemy x y dir`

At 0x45a140, a dead enemy comes back at full health at (x, y), facing `dir`.
The command does nothing if the enemy is alive.

## `SETCHARTRANS` and `GETCHARTRANS`

These read and write a scenario object's opacity, 0-1000, at object +0xf4.
It is the same opacity the object is drawn with (0x45ddd0), so a script can
fade a door or a wall out. They work on objects only. `GETCHARTRANS` leaves
its variable alone for anything else. The opacity starts again from the
scenario file each time the scenario is entered.

## `STARTVISUAL picture`

0x434001 shows a story picture over the screen. It fills in the
loading-screen object at 0x482df4, and 0x417bd0 draws it:

- **Picture.** Each page is a pattern of `System/Common/Pattern/VisualNN.njp`.
  Picture 0 is pattern 4 of `Waiting.njp`.
- **Fade and pages.** Each page fades in over 120 original ticks. After 300
  ticks, a click or a key turns the page, and after the last page the game
  comes back.
- **Waiting arrow.** While the game waits for a click, `WaitIcon.njp`
  pattern 0 steps along at (590, 440), (598, 440) and (606, 440) on a
  15-tick cycle.

If the content has no such picture, the command is skipped. The open
pack has its own pages, painted by `tools/art/story.py`.

## `SETTINGRAIN r g b count`

At 0x41fe20, each call adds `count` raindrops in that color. A script that
wants steady rain runs it every tick. Each drop is set up like this:

- **Start:** x = rand % 1000 - 180, y = -30.
- **Speed:** rand % 1000 + 2000, in hundredths of a pixel a tick.
- **Length:** rand % 10 + 5.
- **Brightness:** rand % 700 + 300, out of 1000.
- **Angle:** 4.712388 - π / (rand % 10 + 10), which falls a little left of
  straight down.

A drop is gone once it reaches y = 479.

## `MEMORIZEMAGIC spell` and `CHECKMEMORIZEDMAGIC spell var`

**`MEMORIZEMAGIC`** marks a spell known by setting player +0x1440 + 4n to 3.
It does not set a level, and the spell casts at level 1. The engine records
the spell at level 1 without putting it on the magic bar, so it casts the same
way. For example, scenario 04100000 teaches Identify (17).

**`CHECKMEMORIZEDMAGIC`** stores 1 when the spell is known, and 0 when it is
not.
