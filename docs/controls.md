# Controls and options

The keys and the Settings Menu follow the original game's manual (its
Controlling Your Character, Shortcut Keys and Game Screen pages) and the
Help window built into `ShadowFlare.exe`. Where the engine's earlier keys
disagreed with the manual, the manual won. Things the engine does not have
yet show a short "not in the game yet" message when their key is pressed,
and are dimmed in the Help window and the Settings Menu.

## Mouse

| | |
|---|---|
| Left click / hold | walk to the cursor, attack the monster under it, talk to a person, pick up an item, use an object |
| Ctrl + left click | run there (while WALK is chosen) |
| Shift + left click | attack in place without walking |
| Right click | cast the chosen spell at the cursor; with the attack chosen (F9) attack the monster under it |

In a shop, a left click on the shop's goods buys into the bag and an item
dropped on the shop is sold. A right click on the goods buys too, as the
original's does (0x44b020): a belt potion goes in the belt first, a mine
joins the mines carried. A right click on an item in the bag sells it in
one go; that is ours (`rightClickSells`), and in original mode a right
click in the bag uses the item as it does anywhere else.

What a click picks when things overlap is set by Click Priority in the
Settings Menu, and how far from the cursor it reaches by Click Range.

The pointer is the original's (`System/Common/Pattern/System.njp`
pattern 0, or 1 with a question mark while Identify waits for an item),
or the open pack's own (`tools/art/pointer.py`). With Click Range on, a
faint square round it shows the range (MINI none, then 12, 16, 24 and 48
pixels either side); it brightens when something is under it, red on a
monster and yellow on an item. The one under it is drawn brighter, with
its name over it: a monster's name sits in a box that is its health (dark
red for what it has left, black for what it has lost) behind its
element's orb; people, objects and items have a navy box, items in their
quality's colour. Neither shows over the panel or a window, as in the
original (0x416bb0, 0x40ee70). A chest, lever, door or item under it also
gets a light gold outline, so what can be used stands out; that is ours,
and original mode leaves it out. In the open pack, `enemies.txt` names the
monsters; its people and objects have no names to show.

## Keys

| Key | | In yet |
|---|---|---|
| F1-F8 | choose the spell for the right button | yes |
| F9 | choose the attack for the right button | yes |
| 1-8 | use a belt pocket item (1-4 the top row, 5-8 the bottom); a right click on it does too | yes |
| B | lay a mine where you stand; the MINE button by the belt does too ([mines.md](mines.md)) | yes |
| P | increased-power mode, once its button by the panel's crossed swords blinks (or click the button); see [magic.md](magic.md#increased-power-mode) | yes |
| Ctrl | run while held | yes |
| Shift | stop where you are; with a left click, attack in place | yes |
| Caps Lock | swap between the two equipment sets | no |
| Tab + click | companion: left click jumps (onto a monster, switch or spot), fetches an item, or shows its status; right click dashes; see [partners.md](partners.md) | yes |
| Space | companion active / inactive (or click its gauge at the bottom left); see [partners.md](partners.md) | yes |
| S | status window | yes |
| I | items window | yes |
| X | special items window | no |
| M | magic window, the STATUS window's other tab; drag spells to its bar for F1-F8; see [magic.md](magic.md#the-magic-window) | yes |
| Q | mission list; see below | yes |
| N | map (the arrow keys scroll it, Enter centers it) | yes |
| R | switch walk / run | yes |
| H | help window | yes |
| Print Screen | screen shot, saved as a bitmap in `ScreenShot/` next to the saved game | yes |
| Esc | close the open window, or open the Settings Menu | yes |

Ours, not in the manual: F10 saves the game and F11 loads it; F12 swaps
between the original's art and ours (with the original files only, see
[content.md](content.md#drawing-the-original-game-with-this-art)); the arrow
keys walk; Page Up / Page Down switch maps, Home goes back to the start
point and J shows the walkability grid (for testing).

Also ours: the scroll wheel zooms the world, 10% a notch, from 100% (the
original's 640 x 480 view) out to 50%, which shows twice as much of the
map each way; scrolling towards you zooms out and away zooms back in. The
panel, the windows, the pointer and the names over characters keep their
size. Darkness shrinks with the world, so zooming out does not let you
see further in the dark. The zoom is kept in `options.txt` (`zoom=`).
Original mode keeps the world at 100% and the wheel does nothing. The
original has nothing on the wheel, and no window here uses it.

Changed from before: F9 used to load (now F11), N and P used to switch
maps (now Page Down / Page Up only), and WASD no longer walks (S is the
status window).

## Missions

Q (or Mission List in the Settings Menu) opens the mission list, laid out
as the original's (ShadowFlare.exe 0x40cea0): every mission given so far,
a closed lock by those still to do and an open one, greyed, by those
cleared, in two columns of 12 on each of two pages (the I and II buttons
at the top). A click on a mission opens its description; a click anywhere
closes it again. Q, Esc or a right click above the bottom bar close the
list.

When a script names a mission (DRAWQUESTNAME, as people do when they give
one), its title shows in brackets at the bottom right for 20 seconds, and
a lock sits at the right edge while any mission is still to do; clicking
either opens the list. Ours lifts both a little to clear the spell bar.

On arriving in an area its name shows at the top left beside a compass,
which shrinks into place, stays for five seconds and fades (SETCOMPASSES).

## Warp gates

As the manual's "Use a Gate": walking up to a town's gate finds it, and a
left click on it opens the GATE window in the left half of the screen,
listing every gate found so far, ten to a page with arrows below for the
others. A click on a gate's name takes the party there. Walking away, a
right click in the window, Esc or opening another window closes it. A
new game knows Remote Town's gate from the start. See
[formats.md](formats.md#the-gate-window).

## Settings Menu

Esc during play opens it; Options on the title menu opens the same rows
without the save and exit ones. Up/Down choose a row, Left/Right or a click
change it, Enter steps through it or does it.

| Row | What it does here |
|---|---|
| Screen Mode at Start | WINDOW or FULL (full-screen desktop). Ours takes effect at once as well as at the next start. |
| Semi-transparent Player | ON draws a faint copy of the hero over walls and trees that hide them |
| Semi-transparent Shadow | OFF draws shadows solid black |
| Display Darkness | ON darkens ruins, caves and towers beyond the light round the hero, lamps and glowing monsters, as the original does |
| Save Image at Game End | not in yet (no load-screen pictures) |
| Click Range | ON: a click reaches things in a square around the cursor; OFF: only right at it |
| Click Range size | MINI, SMAL, NORM, LARG, MAX. The square drawn round the pointer is the original's: none, 12, 16, 24 and 48 pixels either side (0x416bb0). The original picks what a sprite overlapping it is; ours aims at each character's middle, so it reaches further: 12, 20, 32, 44 and 60 pixels. |
| Click Priority | ENEM, ITEM, OBJ., PEOP, COMP in the order a click tries them; click one (or Left/Right) to bring it to the front |
| Eff. Volume, BGM Volume | sliders from 0 (silent) to 10, as the original's. Left/Right move a step; click or drag along the bar. The music changes as it plays; effects take the new volume from the next one. Saved in options.txt. |
| Original Mode | ours: play by the original's rules, bugs included (see `src/logic/rules.h`). Off by default; new games use this, a saved game keeps its own. |
| Art | ours: ORIGINAL or OURS, the art the original game is drawn with; F12 swaps it too. Only shown when playing with the original files. |
| Mission List | opens the mission list |
| Map | opens the area map, as N does |
| Help | the keyboard help window |
| Save and Return to Title | saves, then shows the title menu |
| Save and Exit | saves, then quits |

### Debug tab

Ours, not the original's: debug options and cheats. Tab (or Page Up/Page
Down, or a click on the tab names at the top) switches between the settings
and this tab. Original mode turns every cheat off and dims its row.

| Row | What it does here |
|---|---|
| Experience Rate | multiplies the hero's experience from kills and quest rewards, and spell training (each use counts that many times): 1x, 2x, 5x, 10x or 20x, 1x by default. A cheat. Saved as `xp_rate` in `options.txt`. The original goes up one level at a time and drops whatever is over; above 1x the extra carries over instead, through as many levels as it fills, so a 100% quest reward at 2x is two levels. |
| Show FPS | ON shows the frames drawn in the last second in the top left corner, in every mode. Off by default; saved as `show_fps`. |

To add an option or cheat: give it an `OptionRow` on the Debug tab in
`src/game/options.cpp` and a field in `Settings` (`src/logic/settings.h`). A
cheat must do nothing in original mode.

The options are kept in `options.txt` next to the saves in SDL's settings
folder. Runs with test flags (`--ticks`, `--walk`, `--no-menu` and so on)
ignore it unless given `--options-file <path>`.

## The original's SFlare.Cfg

The original keeps the same settings in `SFlare.Cfg`, sixteen 32-bit
numbers, written at 0x401d10 and read back at 0x401eb0. In file order:

| Offset | Field (object offset) | Read back as |
|---|---|---|
| 0 | screen mode (0xb148) | any non-zero becomes 1 |
| 4, 8, 12, 16 | four on/off rows (0xadc0, 0xadc4, 0xadc8, 0xadb8) | 0 or 1, else 1 |
| 20 | 0xadd0 | ignored: always set to 1 |
| 24 | on/off (0xadd4) | 0 or 1, else 1 |
| 28 | click range size (0xafc4) | 0-4, else 2 (NORM) |
| 32 | on/off (0xafc8) | 0 or 1, else 1 |
| 36 | click priority (0xafcc), five numbers | must be 0-4, each once, else the file is rejected |
| 56, 60 | two volumes (0xb174, 0xb178) | DirectSound attenuation, clamped to -10000..0 |

Which on/off number is which row is inferred from the menu's order, not
traced to the code that draws it.
