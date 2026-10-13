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
| 200 | Moon: MP drain per 10000 of the maximum (row 0) and its percent changes to the companion's 13 stat rows (rows 1-13, one for each row 0-12 of its own table, 0x4136f0) |
| 201 | Berserker: MP drain per 10000 of the maximum (row 0) and its percent changes to 12 stats (rows 1-12) |
| 24 | by the attacker's level in the monster's opposite element - 1: added to a stun's chance and length |
| 70-78 | a spell's stun on a monster of each element (78: none): chance, length and whether it may run past 15 ticks, three columns per level (0x4174b0) |
| 600-621 | each spell's name and description |

A spell's power is table 17 plus the hero's magic attack (at least 1),
its hit table 18 plus magic hit (Fire Ball's cast, 0x4399db). Casting a
spell costs table 16, less what equipment saves, at least 1 (0x44f610).
Spells are trained to level 20 (0x44f6f0); equipment's Magic Level
modifiers add levels up to 30 when cast (0x451e60). Mental Cost modifiers
come off the MP cost, broken items included (0x44f610). A new hero's chant
speed is 80 (0x440f70); Chant Speed modifiers add to it.

### Stuns (0x459690)

A spell that hurts a monster may stun it: tables 70-78 give the chance in
percent and the length in ticks for the monster's element, at the
spell's level, and whether it may last past 15 ticks. When the hero's
level in the monster's opposite element is 1-10, table 24's row for it
adds to both. The monster's own resistances come off. Ice Bolt, Ice Blast
and Dread Deathscythe freeze (their damage block's word 3 is 1): ice
closes round the monster (11000028), one of four blocks of ice holds it
(11000029-11000032, chosen at random) and breaks away when the stun ends
(11000033), all from the controller at 0x42d380. Medusa's stun turns it to
stone (word 3 is 2): it is drawn grey, held still on the first frame of
its hit pose, and can't be stunned again until it wakes; one killed as
stone stays grey.

### Transport (0x43a260, 0x420020, 0x4201b0, 0x420970)

Only where the area allows it (scenario + 0x70). As the cast begins, a
gate opens 500 away towards the cursor, up, down, left or right (whichever
the cursor is most towards); if the way there (a corridor 160 wide) is
blocked, the first of the four ways that is clear. None in the town
itself, nor where its ±80 square is blocked. A new gate replaces the old
one. Its four pieces (OPTION 10000020's Pattern pictures) drop from
height 300 by 50 a tick and brighten 200 a tick, 7 ticks apart; once the
last is down, its animation plays every 30-89 ticks, its owner's name
shows over it when the mouse is on it, and walking into its ±50 square
goes to the town's entry 100. There the gate stands too, and leads back
to where it was cast; its owner coming back closes it. Arriving on an end
does not send the hero straight back. The gate is not saved.

### Moon (0x43d290, 0x4138b0, 0x45f550)

Cast with Heal's casting animation, it turns on, or off if it was on;
either way it costs the casting. While on it drains MP every third tick
like Berserker, at the rate of the level it was cast at, raises the
companion's stats by table 200 and makes it glow (11000040). It goes off
at 0 MP and trains by kills.

### Identify (0x43f8d0, 0x446320)

Plays 11000230 and opens the inventory; the next unknown item clicked in
the bag is identified, which trains the spell. The right button calls it
off, and so does closing the inventory. It can't be cast again while it
waits, and the hero doesn't fight meanwhile.

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

## Increased-power mode (0x4134a0, 0x451a00, 0x4429b0)

Each monster the hero kills themself (not the companion) adds one to a
charge (player + 0x1550). At 50, or 30 while the Spirit Stone (special
item 98000001) is in the bag, the power button over the panel's crossed
swords blinks (Bar.njp pattern 12, added to what is behind, at full
brightness for two frames and 80% for two; 0x4039f0). P or a click on it
(x 25-58, y 408-430; 0x445bd0) starts the mode: the charge empties,
Berserker goes off, and for 900 frames (30 seconds) the hero

- walks, runs, attacks and casts at the fastest step of each speed scale
  (0x450d40, 0x450c60);
- casts every spell two levels higher, at most 20 (0x451e60);
- has defense x 12/10 against blows (0x443cb0);
- can't cast Berserker, Moon or Energy Shield (0x441c00);
- glows: Player/Common/Powerup, one 30-frame chart added to what is
  behind, a frame each tick at the hero's feet (0x4448b0).

The charge is not saved. Not in yet: the sound every 15 frames, and the
hunter's 1-in-3 special attack (0x438a90).

## The magic window (0x407a60, 0x447790)

M (or the MAGIC tab of the STATUS window) opens it on the left of the
screen (Status.njp pattern 6), six spells a page over four pages. Each
learned spell shows its icon (MagicIcon.njp, spell + 2), name, the level
it is cast at (gold when power mode raises it), uses towards the next
level out of table 27 (Max at 20), MP cost (table 16) and power (table
17; a percentage for Heal, Energy Shield, Sonic Blade, Magic Shield and
Counter Burst; nothing for Transport, Moon, Berserker and Identify).
Hovering a name shows the description (tables 600-621) under the mouse.

The bar along the bottom is the one F1-F8 choose from. Drag a spell's
icon, or one already on the bar, onto a slot: it leaves any other slot
and goes there. Let go anywhere else and nothing changes. S, Q, the
STATUS tab, a shop and Esc close the window. `--magic-window[=page]`,
`--power-charge n` and `--power` open or start them for screenshots.

## In the engine

`src/logic/magic.h` has the rules and tables, `src/game/spells.cpp` the
casting. F1-F8 choose a spell from the bar and the right button casts it
at the cursor; casting a shield or Berserker again turns it off. The
status window shows MP and the magic stats, and the HUD an MP bar under
the health bar.

### Effects

Each spell plays the original's effects, `Character/OPTION/<number>`
animations, in the original's order and timing. The original runs 30 ticks
a second and the engine 20, so spells run 1.5 of the original's ticks per
engine tick, and an effect shows one frame per original tick (its
animation speed, effect word 28, is 1000 per mille). Missiles move table
35's speed each original tick. The original's spell controllers (0x42b1c0
to 0x42eeb0) say what each spell spawns; R is the tick the spell leaves
the hand:

| spell | what happens | hits with |
|---|---|---|
| Fire Ball | 10000012 on the caster; at R a missile 10000010 from 180 in front, ±50 square, until it hits or meets a wall | 10000011 |
| Ice Bolt | the same with 11000027 and 10000040 | 11000026 |
| Plasma | table 205 bursts, one every 4 ticks from R, at 250, 450, ... in front: lightning 10000030 (one of 4 charts) hurting a ±100 square, splash 10000031 and glitter 10000032; a blocked spot stops the line | 11000003 |
| Hell Fire | ring 10000002 at tick 3; at R sprays 10000000 (charts 0 and 1) and one tick of damage within 150 of the caster's square | 10000001 |
| Ice Blast | ice 10000051 at tick 3; crystals 10000050 when it ends (L); damage within 150 of the caster at L+4; mist 10000052 at L+15 | 11000026 |
| Heal | 11000060 on the caster (casting chart 11) | |
| Earth Spears | table 206 spears 10000060, one every 8 ticks from R, at 250, 550, ... in front, each hurting a ±150 square | |
| Flame Strike | 10000012; at R table 204 Fire Balls evenly round the caster from the aim, homing 20° a tick, for 90 ticks, ±80 | 10000011 |
| Dread Deathscythe | 11000027 and table 204 blades 10000080 150 out in a fan (144° at the most blades) until R; then a missile 10000081 from each, 90 ticks | 11000026 |
| Lightning Storm | table 204 rays round the caster; 4 rings of Plasma bursts every 4 ticks from R, 350 to 950 out | 11000003 |
| Medusa | at R a missile 10000070 from 180 in front, ±50, like Fire Ball; its stun turns monsters to stone | 11000050 |
| Sonic Blade | at R a cut 10000090 at the spell's speed for 7 ticks, hurting all in ±80, gone after its first hit | 11000200 |
| Mud Javelin | at R a missile 10000110 (±80); where it ends, 10000111 hurting ±240 | |
| Elemental Strike | 11000210; at R table 207 orbs 10000100 round the caster, homing; where each ends, 12000000, 11000033, the Plasma burst and 10000060, 4 ticks apart, each hurting ±240 | 10000011, 11000026, 11000003, 11000000 |

Magic Shield shows 11000241 when it takes a blow and Counter Burst
11000251 when it strikes back. Tables 204-207 count, by level, the
fireballs, blades and rays (204), Plasma's bursts (205), the spears (206)
and the orbs (207). The open content paints its own art for all of these
(`tools/art/effects.py`); with the original's files the engine plays the
original's.

Ours rather than the original's, until they are read from the exe:

- Heroes and monsters are hit in a ±50 square (the original reads each
  one's judge rect), and a damaging effect hurts each monster once.
- Plasma, Lightning Storm and Earth Spears test one point for a wall
  where the original tests a square.
- A missile that meets nothing stops after 6000 world units.
- Effects are sorted in with the map's objects and characters by where
  they stand, but the original's effect heights are not used for it.
- A shield or Berserker turning on plays 11000100 (which effect the
  original plays is not known), and a faint ring shows while it is on.
- Explosion sends the companion to blow up where it is cast
  ([partners.md](partners.md)).
- Transport's gate tests the ground every 20 units for walls where the
  original asks its collision map for the whole rectangle, and the hero
  steps into a gate with a ±30 square (the original uses their judge
  rect).
- The mouse pointer is the system's: Identify's pointer is not shown.
- Monster spells land halfway through their animation, like blows.
- The hunter's special attack in power mode needs bows' arrows, which are
  not in yet.

## Original mode

The menu's Original Mode (and `--original`) plays by the original's rules
with none of our fixes; it is off by default. Each fix is a switch in
`src/logic/rules.h`, and original mode turns them all off:

| switch | ours | original |
|---|---|---|
| `shieldsCostTheirOwnMp` | Magic Shield and Counter Burst cost their own MP | they cost the chosen spell's MP |
| `partnerStatusShowsMagicEvasion` | the partner's status sheet shows its magic evasion | it shows its defence there ([partners.md](partners.md)) |
| `explosionUsesItsOwnLevel` | the companion's Explosion stuns at Explosion's level | at Elemental Strike's level ([partners.md](partners.md)) |

Data the engine used to misread, such as the monster stat words, is
corrected in both modes.
