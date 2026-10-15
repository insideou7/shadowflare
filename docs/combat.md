# The Warrior's combos and spin, and the Elixir

How ShadowFlare.exe runs the Warrior's special attacks (player states
11-18 in the state machine at 0x443a80) and the Elixir that brings a
fallen hero back, and how the engine copies them. The pure rules are in
`src/logic/combat.*` (unit tested in `tests/logic_test.cpp`); the game
side is `src/game/combat.cpp`.

## Starting one

A Warrior (job 6) whose plain attack would be a sword's, a great sword's
or an axe's (states 8, 9 and 10, by Item.Ibn weapon subtype 0, 3 and 1)
does a special attack instead:

- With the attack chosen (F9), the right button turns the attack into
  states 11, 12 or 13 (0x441c00).
- Holding the left button, the right button does it whatever spell is
  chosen (0x448740).

Subtype 0 (one-handed sword) and 3 (two-handed sword) give a three-blow
combo, subtype 1 (axe or mace) the spin. Staffs, rods and bowguns have
none. While the hero is busy with another move (+0x2dc: casting, a stun,
a shot, a swing or a flinch still playing) the click does nothing.

## Speed

The step 0-9 comes from table 4 at attack speed / 32, plus 1, or 9 in
power mode (0x450c60). Each original tick the combo's frame is the
tick count times 0.8, 0.9 ... 1.7 for that step; the spin's 0.6 ... 1.5.
The engine runs these on the original's 30 ticks a second.

## The combo (0x4364e0, 0x436c20, 0x4372b0)

Three blows, each its own chart: 5, 7 and 8 for the sword, 15, 17 and 18
for the great sword. Each blow:

- lunges along the aim 0, 10, 20 ... 60 units a tick until it lands
  (+0x1614);
- lands on the chart's frame flagged 0x40, with the shout 0x60, 0x61 or
  0x62 (the heroine 0x63-0x65), and the weapon's swing sound at tick 6;
- hits every living monster within reach (264 units, as the plain swing)
  and two directions either side of the hero's facing (0x4417f0), each
  after a hit roll against its evasion;
- deals the hero's blow damage, rolled 9-11 tenths of the attack, or
  10-12 for the third blow (0x412a40);
- may stun it: chance and length from table 25 by damage x 50 / its
  maximum health, plus the weapon's modifiers 14 and 15, plus table 24
  by the hero's level in the monster's opposite element. Blows 1 and 2
  are not capped at 15 ticks; blow 3 only if the weapon has modifier 16;
- sends back life (absorption), wears the weapon (30%) and thuds (6).

The next blow follows as soon as one ends. The open pack's hero has no
combo charts; each blow then plays the attack chart.

## The spin (0x437910)

Chart 21 winds up facing the aim. Chart 22 (its ninth, direction-less
slot, 8 frames) turns round three times, starting where the wind-up's
facing leaves off. Chart 23 comes out of it. While it turns the hero
moves 30 units a tick along the aim, the swing sound plays every 8 ticks
and a blow every 6 hits every monster within reach all round. Its stun
chance is +100 and its length the weapon's +10, capped at 15. Without
these charts the attack chart stands in, the hero turning on its strike
frame.

## The hero stands firm

In states 11-14 (combo blows 1 and 2, and the spin) blows that are not
spells do not stun the hero (0x443cb0); a spell's stun stops the move.

## The Elixir (0x443cb0)

When the hero's health falls below 1 and special item 98000000 (Elixir)
is in the bag, it is used up instead: health and mental power fill, voice
0x11 plays and controller 0x521c (effect 21020, OPTION 11000060) shows
on the hero.

## MISS!! (0x417a60, controller 20012 at 0x42c750)

Whenever a blow, a shot or a spell fails its hit roll, "MISS!!" (the one
picture in Character/OPTION/11000011's Pattern bank) pops up where the one
it missed stands. The exe calls 0x417a60 from the hero's swings, combo
blows, spin and plain attack (0x435e60, 0x4364e0, 0x436c20, 0x4372b0,
0x437910, 0x439140), from a monster's blow (0x45aaae) and from the code
where shots, spells and traps' effects reach whoever they hit (0x45e44c).
The engine shows it on the hero, on monsters, and on the companion when
a monster's blow, shot or spell or a trap misses it (the companion's
part is inferred: those calls pass a character, as they do for monsters).
The companion's own bites and the mines' blasts show none here; the
exe's last call, at 0x460330 in the companion's code, is not placed yet.

It starts 400 high and hops: up by a tenth of its speed each tick, the
speed starting at 500 and falling by 100 a tick. Each time it comes back
down to 400 it bounces, at 300 and then 200; from its third landing it
dims by a tenth a tick and is gone after ten.

