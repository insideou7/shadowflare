# Partners

How ShadowFlare.exe runs the hero's animal companion, and what the engine
does with it. The game's manual (`manual/partner.html` in the game files)
describes the same from the player's side.

## Kinds and stats

Table.Tbd table 60 lists the six kinds: 0 Kerberos, 1 Gravity, 2 Dune,
3 Fang, 4 Harley and 5 Hawk. Column 0 is the name, column 1 the
`Character/PARTNER/%08d` folder (the four dogs share folder 0) and
columns 2-4 the red, green and blue tint in thousandths, which tells the
dogs apart.

Each kind's stats are in table 800 + kind: 19 rows, one column per level
(35 levels). Rows 0-12 add up the columns up to the level (column 0 is the
base, the rest what each level adds); rows 13-17 are the value at the
level.

| row | stat | row | stat |
|---|---|---|---|
| 0 | attack speed | 8 | evasion |
| 1 | walking speed | 9 | magic attack |
| 2 | running speed | 10 | magic defence |
| 3 | max HP | 11 | magic hit |
| 5 | attack | 12 | magic evasion |
| 6 | hit | 13 | element |
| 7 | defence | 18 | experience to the next level |

Rows 14-17 drive the jump skill (see Skills). The hero's walking
speed stat is 128, so a partner moves `70 * stat / 128` world units a
tick next to the hero's 70.

## Levels

The hero keeps every kind's level and experience, so a swapped-out
partner keeps what it learned. Each monster killed by the hero or the
partner, while the partner lives, gives it one experience point. A
partner can't go past `min(35, hero level / 3 + 2)`; at the cap it keeps no
experience. A level-up fills its health.

## Following and fighting

The partner starts each map next to the hero. Further than 4000 from the
hero it jumps to them; from 600 it runs, from 160 it walks, and closer it
waits. It is ACTIVE or INACTIVE: Space, or a click on its gauge at the
bottom left, switches. A new hero's partner is INACTIVE.

- **ACTIVE**: it goes for the nearest monster within 1200 of the hero and
  bites when the gap between their boxes is 150 or less. Hits use the
  same hit chance as the hero's, and damage the spell formula
  (`docs/magic.md`) with its attack and element. Monsters' blows can reach
  it.
- **INACTIVE**: it only follows, monsters can't hurt it, and it heals a
  thousandth of its health each tick.

When its health runs out it falls and comes back next to the hero after
900 ticks, or 600 while the hero carries a Companion Stone (special item
98000002). Its health carries over from one map to the next. `HEALLIFE`
fills it too, but does not bring a fallen partner back.

Waiting by the hero (0x461230), it counts the original's ticks; at 180 a
kind whose table 60 column 6 is set (all but the Hawk) plays chart 9, a
dog's scratch or yawn, with voice 0x58, and at 210 it stands again and
the count starts over. The open pack's dogs have no chart 9 and just
stand.

The gauge is Bar.njp pattern 30 (frame, at 0, 395), 29 (the bar) and 31 or
32 (ACTIVE or INACTIVE, at 56, 392). The bar blinks below 30% health.

## Scripts

- `GETPARTNERKIND var`: the kind the hero has.
- `CHANGEPARTNER kind`: swaps partners, free; the new one comes at full
  health. Remote Town's kennel (people 12010000-12010003) offers the four
  dogs.
- `GETPARTNERLIFE life max`: its health and maximum, -1 and -1 while it is
  down; with no partner the variables are left alone. Healers use it.
- `TALKPARTNERSTATUS kind answer`: a box over the speaker with that
  kind's status sheet; `answer` is set as for `TALK` when it closes.

The original's status sheet prints the partner's defence on the "M Evasion
Rate" line. We print its magic evasion unless original mode is on
(`partnerStatusShowsMagicEvasion` in `src/logic/rules.h`).

## Skills

Holding Tab, a click gives the partner an order (0x441c00, 0x448b50).
Orders on the ground wait out a 45-tick pause after the last one.

- **Left click on a monster, or on open ground: jump.** It crouches
  (chart 6), lands on the spot and gets up (chart 7, facing the camera)
  (0x460420). Landing tries every monster in a square of row 17 of its
  table around it; the blow does no damage (its power is 0), but each
  one hit may be stunned: row 14 is the chance, row 15 the length in
  ticks (at most 15 unless row 16 is set).
- **Left click on a switch: jump onto it**, pressing it. Switches are
  Mct objects whose word 5 is 1 (inferred from the click code).
- **Left click on an item: fetch.** It runs to the item (giving up past
  4000 from the hero) and the hero picks it up once it is in reach
  (0x4624e0).
- **Left click on the partner** while it stands: its status sheet.
- **Right click: dash**, only within 160 of the hero (0x4490b0). It winds
  up for 10 ticks, then runs toward the click starting at speed 30 and
  gaining 8 a tick until tick 27. From tick 12, every fourth tick it hits
  what is in its box grown 1.9 times, stunning with chance 100 and
  length 10 (at most 15), and pauses 5 ticks after a hit (0x460a00).
  From tick 18 it trails a **speed streak**: the run chart's third layer
  (Animation.Caf chart 2, layer 2), long grey pictures drawn additively
  (cell status 16) at opacity 800 and 700 on alternate frames, in front of
  the body and not recoloured by the coat tint (only layer 1 gets it,
  0x45ff50). The partner's layer switches (+0x3c) have layer 2 off; the dash
  turns it on for the ticks it is flat out (`+0x158 > 0x11`, not while a
  hit holds it) and 0x45ff50 turns it off again after every tick, so an
  ordinary run never shows it. The open pack paints its own streak
  (tools/art/streak.py) as layer 2 of chart 2.

**The hero's Explosion spell** (spell 20) sends the partner to the point
cast at, if it is up, not jumping or dashing and past its 45-tick pause
(0x43fcc0). It jumps there as above and blows up (0x461c40), with the
fire spray Hell Fire uses (effect 10000000, both charts; 0x42f890): every
monster in a square 320 each way that it hits (its hit against their
evasion) takes the spell's blow, with power 1, so 1 damage, and is
stunned as Table.Tbd tables 70-78 say for the spell at its level (one
table per monster element, three columns per level: chance, length, and
1 to lift the cap). Explosion's are chance 800, 10 ticks at level 1 up to
19 at level 20, capped at 15. Each monster hit trains the spell. The
original reads the level of spell 21, Elemental Strike, instead
(level 1 for a hero who does not know it); we use Explosion's own unless
original mode is on (`explosionUsesItsOwnLevel`).

A blow landing on an object whose Mct word 6 is not -1 goes to the
monster with that id instead (0x460420 looks up character 14000000 +
the word). Only some parts of object 2 in scenarios 04120000 and 04900002
have it, linked to monster 30000, which neither scenario places, so it
does nothing in the shipped game.

A monster's stun chance and length are cut by its Mct enemy words 61 and
62, and word 63 lifts the 15-tick cap (all three inferred from where
0x459690 reads them). An attacker whose element is the monster's element
xor 1 adds 15 to the chance and 9 to the length (table 24 row 5).

## Meat and items

Meat (consumables 20000000-20000003) heals the partner 500, 1000, 2000
or 3000: an item heals the partner (effect words 5 and 6) only when it
did nothing for the hero, and the partner must be up and hurt; otherwise
the meat is kept (0x44a240). Items the hero wears with modifiers 30-38
add to the partner's stats (see formats.md).

## Open content

An open pack's `partners.txt` stands in for the tables (see content.md).
The pack's Remote Town has the kennel's dogs and the small dog, and Cold
Svalt Town its dog, with the original's menus in our own words. The
sixth kind, the hawk Kestrel (Character/PARTNER/00000002), is offered in
scenario 13900005, as the original offers its Hawk in 03900005.

## Code

`src/logic/partner.*` holds the tables and the hero's record (unit
tested); `src/game/partner.cpp` moves, fights and draws it. Saves keep a
`partner=kind,active,revive ticks,hp,` line followed by each kind's level
and experience.
