# Mines

The hero lays land mines with **B** or the **MINE** button beside the
belt, as in the original. This page says how ShadowFlare.exe does it, as
read from its code with Ghidra (addresses are in ShadowFlare.exe), and
where the engine has it (`src/logic/mines.*`, `src/game/mines.cpp`).
Times are the original's ticks, 30 a second; the engine runs 1.5 of them
per frame of its own, as it does for spells.

## Carrying mines

- A new hero has **5** mines (0x4414db) and can carry **10** (0x4412c6,
  player +0x160).
- Worn items with the Mine Count modifier (28) raise the most; ones with
  Mine Effect (27) add to the blast's power. The stats code (0x44ea60)
  keeps both, and cuts the mines in hand down to the most when an item
  comes off (0x450183).
- Mines never go in the bag. A mine on the ground (special item 1, "Mine",
  2x2, price 100) adds one to the mines in hand when the hero picks it up,
  or stays on the ground when the hero has the most already (0x44a0e2).
  Monsters drop them from the same lists as potions (Table.Tbd table 31;
  ours: `pick=special,1` in content/drops.txt).
- A mine bought in a shop goes onto the count too, and isn't sold while
  the hero has the most already. The original's right-click buy
  (0x44b020) does this; its left click takes the mine into the hand and
  charges for it when it is put in the bag or on the mine picture, which
  raise the count and refuse a full one (0x446320 cases 9 and 0xb). We
  buy with a left click straight onto the count. A mine a dog fetches
  also goes onto the count.
- Saves keep the count (`mines=`). A save from before mines gets a new
  hero's five. Mine items in a save's bag (from before mines, or bought
  before buying counted them) join the count while there is room.

## Laying one

B (0x443073) and a click on the MINE button (x 496-511, y 424-439,
0x445fc5) do the same: with a mine in hand and no wait running, one mine
is taken, a wait of 10 ticks starts and the mine's controller (0x42bd40)
is made where the hero stands, keeping the hero's level.

## The mine

- On the ground it is OPTION 1000, a Pattern bank of two pictures:
  0 as laid and 1 armed, shown from tick 40.
- From tick 45 a monster within 150 (a square, as the original judges)
  sets it off. Left alone it goes off by itself at tick 300.
- The blast (OPTION 1001) hurts every monster within 600, once, with a hit
  of 9999 (so only the 98% cap on hits can miss).
- Power is table 23 of Table.Tbd at the hero's level when it was laid
  (40 at level 1, up by 10, 15, 20 and 30 a level in steps, 2000 at 100)
  plus the Mine Effect worn, at least 1. Damage is a blow of that power
  with no element levels: power x (9 + 0..2) x table 7 / 100, less the
  monster's defence, at least 1 (0x412a40).
- Each monster hit shows one of OPTION 11000000, 11000001, 11000002 or
  11000009, at random.
- After the blast, every other tick from 12 to 40 two puffs of smoke
  (1002, 1003) go up in a widening ring, 50 further out each time. At
  tick 12 four pieces of the mine (1005-1008), each with a small fire on
  it (1004), fly out at 20 a tick for 60 ticks, thrown up at 150 and
  falling 10 a tick, bouncing lower when one lands. The controller ends
  at tick 80.

All the art for these numbers in content/ is ours: the mine is drawn by
tools/art/items.py (`placed`) and the effects by tools/art/effects.py.

## Not yet known or not yet done

- Nothing found in the exe counts the 10-tick wait between mines down
  (0x48d5d8 is only set and tested); the engine counts it down each tick.
- That picture 1 of the Pattern is the armed look (object word 29) and the
  scale from a piece's height to the screen are read from the code's shape,
  not checked against the running game.
- No rule here differs from the original, so original mode changes nothing.

## Checked against the original

- Sounds (0x42bd40): the mine beeps 54 (0x36) every 20 ticks from tick 40,
  and the blast plays 29 (0x1d), then 6 once when it hits something. The
  engine plays all three (src/game/mines.cpp).
- Kills: the blast's hit carries the hero as its owner and kind 0, a blow,
  not 3, a spell (0x42bd40 fills the hit with 0, 0 and the hero's number;
  spells fill 0, 3). So the kill counter (0x4134a0) counts a mine kill
  like a blow with whatever is in the hero's hand: by its weapon kind
  toward the class offer, as magic with a staff or rod, and only in the
  total with bare hands. It also counts toward power mode. The engine does
  the same (Game::hurtEnemy with bySpell false).
