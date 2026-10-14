# Bowguns, the hunter's volley and monsters' shots

How ShadowFlare.exe shoots bowguns (Item.Ibn weapon subtype 5) and how the
engine copies it. Addresses are in ShadowFlare.exe. The code is in
src/logic/archery.cpp (the rules, unit tested) and src/game/archery.cpp
(the shots in the game).

## The weapon's shot words

A weapon's Item.Ibn record holds five words for what it shoots. Swords
leave them at 0.

| Offset | What | Engine |
|---|---|---|
| 0xfc | shot kind: 0 stone, 1 arrow, 2 fire arrow, 3 ice arrow | `ItemDef::shotKind` |
| 0x100 | spread (see below) | `shotSpread` |
| 0x104 | speed in world units per tick | `shotSpeed` |
| 0x108 | turn (not used by any bowgun) | `shotTurn` |
| 0x10c | pierce: the shot flies on through what it hits | `shotPierces` |

The open pack writes them as `shot=kind,spread,speed[,turn[,pierce]]` in
items.txt (see [content.md](content.md)).

## Pulling the trigger (0x437fe0)

FUN_00450630 maps the weapon's subtype to the hero's attack state, and a
bowgun gives state 0x14. That state plays hero chart 10, the shooting
chart: 17 frames, with the shot leaving on frame 3 (the frame whose status
has bit 0x40). Unlike a sword, the hero shoots from where they stand at any
range, turning to face the target or, with no target, the mouse.

- **Attack.** A hunter shoots with all of the hero's attack. Anyone else
  shoots with 40% plus (hunter levels x 50 / 30), cut to a whole number,
  and never more than 90%. Hunter levels are the level-ups taken as a
  hunter (0x450f80 counts the class bytes at player + 0xec).
- **Hit.** The shot uses the hero's hit rate against the monster's evasion,
  through FUN_00413e00, the same roll as a blow.
- **Durability.** Each pull wears the bowgun by one point (word 0xbc), no
  matter how many shots fly. A broken bowgun shoots nothing, but the hero
  still goes through the motion.
- **Spark.** One spark is chosen for the whole pull. A stone shows one of
  11000000, 11000001, 11000002 or 11000009 (rand() % 4). An arrow shows one
  of 11000020-22 (rand() % 3). These are effects 21000-21003 and 21007-21009
  as 0x42b860 plays them.

### The spread

The shots fan out around the line to the target. Each one starts 200 units
out from the hero and 350 up.

| Spread | Shots |
|---|---|
| 0 | one |
| 1 | two, 8 degrees either side |
| 2 | one, facing its own way |
| 3, 4 | three, 15 degrees apart |
| 5, 6 | five, 10 degrees apart |
| 7, 8 | seven, 8 degrees apart (the outer pair at 24) |

With an odd spread every shot is drawn facing the center shot's direction.
With an even spread each shot faces its own way. No item uses the even
values above 2. Spread 1 is a quirk of the loop: its counter doubles as the
shot's "face yourself" word, so the first shot keeps the center's direction
and the second faces its own.

### The shots (0x429ec0, 0x42a300, 0x45e1e0)

Stones are Character/OPTION/00000001. Arrows are OPTION/00000000, the same
arrow traps shoot ([traps.md](traps.md)). The arrow has four layers: 0 its
shadow, 1 the shaft, 2 flame and 3 frost. The pointer at object + 0x3c is
the layer-visible array. A fire arrow shows layer 2, an ice arrow layer 3,
and a plain arrow neither. The engine keeps the hidden layers in
`Effect::hiddenLayers`.

Each tick a shot works in this order:

1. It hurts what is in its ±30 box. The mask is 0x14, monsters and objects.
2. It checks for a wall.
3. It moves.

A shot that does not pierce hits the nearest monster, then it is gone. One
that pierces hits each monster once, keeping a list of what it has struck.
The blow plays voice 7 (the arrow's sound) and does the hero's blow damage
with the shot's attack.

## The hunter's volley (0x438a90, 0x42b9e0)

A hunter in power mode ([magic.md](magic.md)) who starts a bowgun attack
sets off the volley when rand() % 100 is under 33. It needs at least one
living monster within 2000 units of the hero on both axes. Otherwise the
pull is an ordinary shot. The hero plays the same chart, faster: the
volley's attack-speed table is 0.6, 0.9, 1.2, 1.3, 1.5, 1.7, 1.9, 2.2, 2.7
and 3.0.

On the shooting frame, every monster in reach gets one arrow (at most 100):

- **The arrow.** It is missile 9000 (Character/OPTION/00009000): 15 frames
  with no direction, a glow added to the screen over a solid arrow.
- **The wait.** Each arrow waits rand() % 15 ticks.
- **The fall.** Then it falls over ten ticks. On tick t (0-10) it sits
  (10 - t) x 100 + 1 to the right of its target, the same distance up the
  map, and 4000 - 400 x t high, following the target as it moves.
- **The strike.** At tick 10 it strikes only that monster (box ±5), with
  the hero's full attack and hit rate. It shows effect 20006 (OPTION
  11000004).
- **The pull.** A volley wears the bowgun by one point, the same as any
  pull.

## The status window (0x405750)

When a non-hunter has a bowgun in hand, the status window shows their attack
already scaled by the percent above, in the tone for a raised stat.
Hovering it shows "Revision of Attack (Not for Hunters):-N%" in a black box
with a white edge. The engine draws both the same way.

## Monsters' stones and arrows (0x45a2f0)

Slingshot goblins (Character/ENEMY/00000002) throw stones and dark elves
(00000010) shoot arrows. Words 44-48 of a monster's scenario record say
what it throws (see [formats.md](formats.md)):

| Word | Monster + | What | Goblins | Elves |
|---|---|---|---|---|
| 44 | 0x184 | shot controller, -1 for none | 1 | 0, 4, 5 or 7 |
| 45 | 0x190 | height | 250 | 250 |
| 46 | 0x188 | speed a tick | 50 or 60 | 80-115 |
| 47 | 0x18c | how far out it starts | 200 | 200 |
| 48 | 0x194 | the attack that throws | 0 | 0 |

The dragons and the fallen angel (12 and 24) have kind 6 on their second
attack.

- **The throw.** As the attack whose number is word 48 begins, the
  monster aims at the hero where they stand (+0x260). On each frame of its
  chart with status bit 0x40 (frame 27 of the goblin's 50), it hands a shot
  to 0x42fdc0 instead of striking. The shot starts word 47 out along that
  line, word 45 up, and flies word 46 a tick. It carries the attack's
  power and hit (words 23-25 and 29-31) and the monster's element.
- **The kinds** (0x429ec0, 0x42a300), the same controllers trap shots use
  ([traps.md](traps.md)):
  - 1 is the stone, Character/OPTION/00000001: 40 frames of a small
    turning pebble with no direction.
  - 0, 4, 5 and 7 are the arrow, OPTION/00000000, facing where it flies.
    0 and 7 hide the flame and frost layers, 4 shows the flame, and 5 the
    frost.
  - 6 is OPTION/00000004 with a 320-unit box; the others have a ±30 box.
  - All of them stop at walls and are spent on the nearest one in their
    box.
- **The hit.** It hurts players, companions and objects (mask 0x13).
  The hero takes the attack's blow, as from a melee strike, and shows a
  spark: 11000020-22 for an arrow, else 11000000, 01, 02 or 09. A fire
  arrow (4) also plays effect 20000 (OPTION 10000011) on them, and an ice
  arrow (5) effect 21013 (OPTION 11000026). The shot's sound is voice 7,
  or 20 for kind 6.
- **Freezing and stone.** Kinds 5 and 7 also set hit word 3 to 1 and 2.
  The blow may then stun the hero by table 25 (by the share of their
  health it took, resisted by the gear's stun modifiers, at most 15
  ticks, pushed back unless the gear is steady). Word 1 freezes them in
  ice for the stun (OPTION 0x5217 + one of four blocks, voice 0x77);
  word 2 turns them to stone, grey and still on the hit pose's first
  frame (+0x324, +0x15f0, flag 0x4000). A Warrior's combo blows 1-2 and
  spin are not stopped (see combat.md).
- **Not copied yet.** Kind 6 names effect 21023 in its shot, use not traced. Words
  52-60 go into the shot's hit struct too and are not decoded.

The engine follows this in `Game::throwShot` (src/game/traps.cpp), with
the shot run as a trap's. The open pack's slingshot goblins have
`shot=1/250/50/200/0` and the `thrower` AI list (content/ai.txt), which
tools/content/caster_ai.py gives any monster with a shot. Their charts
have no 0x40 frame, so the stone leaves halfway through the attack.

## Ours

- A shot that leaves the map's walkability grid is removed. The original
  flies it on into nothing.
- The open pack's hero has no shooting chart, so it plays the punch chart
  instead. The open pack's one bowgun, the Wood Bowgun (weapon 1000010,
  sold by Remote Town's smith and dropped from item level 3), shoots one
  arrow like the original's; fighting with it is how an open-pack hero
  earns the hunter's class.
- A monster's shot that misses is also removed once it leaves the
  judge grid.
- Nothing here changes the original's rules, so original mode has nothing
  to switch.
