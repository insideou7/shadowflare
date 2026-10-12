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

Rows 14-17 drive the jump skill, which is not in yet. The hero's walking
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

## Not in yet

The skills on TAB plus a click (jump, fetch, switch, dash), the item
modifiers that raise partner stats, and meat that heals it. Open content
has no partner tables, so open-pack heroes play alone.

## Code

`src/logic/partner.*` holds the tables and the hero's record (unit
tested); `src/game/partner.cpp` moves, fights and draws it. Saves keep a
`partner=kind,active,revive ticks,hp,` line followed by each kind's level
and experience.
