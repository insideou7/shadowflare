# Change Color

`CHANGECOLOR` (script command 72) opens the Change Color window
(ShadowFlare.exe 0x40f9e0): the hero dyes the weapon, shield and body
armor they wear one of 16 colors, and sees it on a picture of themselves.
The engine's version is `src/game/change_color.cpp`; `--change-color`
opens it at once, for testing.

## The window

- Three tabs pick the item: weapon (player + 0x4f4), shield (+ 0x4f8)
  and body armor (+ 0x4ec). A tab whose item is not worn cannot be picked.
- The 4 x 4 swatches start at (0x14c, 0xbb), 32 pixels across and 24
  down. A click dyes the chosen item at once.
- Restore puts the item's own color back (dye -1).
- A 3 x 3 grid of 33 x 31 cells from (0xd8, 0xb4), over the picture,
  turns the hero to face that way; the middle cell does nothing.
- The picture of the hero stands at (269, 244), in the fighting stance of
  an axe (chart 0x18) or a great sword (0x19) when they hold one.
- OK keeps the colors. Cancel, Escape or a right click puts back the ones
  the window opened with (0x410360).
- Clicks above y 0x19c belong to the window; the game panel still works.

Status.njp patterns: 102 the window at (160, 144), 105 Restore, 106 OK
lit, 104 Cancel lit, 108 / 107 / 103 the weapon, shield and armor tabs,
109 a swatch (tinted with the dye).

## The dyes

The 16 dyes are one table at 0x475bf0 (each row of it is the same), in
thousandths of red, green and blue; `kDyes` in `src/logic/inventory.h`
holds them. The weapon's dye is kept at player + 0x348 and the armor's at
+ 0x320. A dye multiplies the item's layer when the hero is drawn. With
no dye an item uses its own tint, the three words after its hero layer in
Item.Ibn. SDL cannot brighten a texture, so strengths above 1000 are drawn
as 1000.

Saves keep the dye as the pair `100,<dye>` in the item's modifier list.

## Open pack

`content/System/Game/Pattern/ChangeColor` holds the window's pictures in
the order window, Restore, OK lit, Cancel lit, the three tabs and the
swatch. `tools/art/cards.py` draws it.
