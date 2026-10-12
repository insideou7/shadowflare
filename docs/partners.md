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
  (0x460420). Landing hits every monster in a square of row 17 of its
  table around it, and each one hit may be stunned: row 14 is the chance,
  row 15 the length in ticks (at most 15 unless row 16 is set).
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

## Not in yet

The Explosion order (command 6), and switches' linked objects (object
word 6). Open content has no partner tables, so open-pack heroes play
alone.

## Code

`src/logic/partner.*` holds the tables and the hero's record (unit
tested); `src/game/partner.cpp` moves, fights and draws it. Saves keep a
`partner=kind,active,revive ticks,hp,` line followed by each kind's level
and experience.
