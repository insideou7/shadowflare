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
dropped on the shop is sold. A right click on an item in the bag sells it
in one go; that is ours (`rightClickSells`), and in original mode a right
click in the bag uses the item as it does anywhere else.

In the item window, a right click on a weapon, armour or accessory in the
bag wears it, and what was worn there goes back in the bag (where the new
item was, if it fits). A right click on a worn item takes it off into the
bag. That is ours too (`rightClickEquips`); in original mode a right click
in the bag only uses consumables and one on a worn item does nothing, so
items are worn by picking them up and dropping them on the figure.

With a warehouse open, a right click on an item in the bag puts it in the
warehouse (the page in view of the Giant Warehouse), and a right click on
an item in the warehouse puts it in the bag, each in its first free place.
Gold still asks how much. That is ours (`rightClickStores`); in original
mode a right click on either only uses a consumable. Holding the pointer
over a warehouse item shows the same box as in the bag, in both modes.

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
| Caps Lock | swap between the two weapon sets (also the I and II tabs under the weapon and shield slots; see [formats.md](formats.md#weapon-sets-the-giant-warehouse-and-the-gold-split-box)) | yes |
| Tab + click | companion: left click jumps (onto a monster, switch or spot), fetches an item, or shows its status; right click dashes; see [partners.md](partners.md) | yes |
| Space | companion active / inactive (or click its gauge at the bottom left); see [partners.md](partners.md) | yes |
| S | status window | yes |
| I | items window | yes |
| X | special items window (quest items, gems, stones and cards, medicines); see [formats.md](formats.md#the-special-items-window) | yes |
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

Also ours, after Diablo: holding Alt shows the names of all the items on
the ground, in their quality's colour, in the same boxes as the name under
the pointer. Where names would cover each other they stack upward, the
item nearest the front keeping its place, so a pile of loot reads as a
list. A click on a name goes for that item (walks there and picks it up),
before anything else under the pointer. L keeps the names on without Alt
and turns them off again; that is kept in `options.txt`
(`item_names=`). Original mode shows neither. The original has no such
key: ShadowFlare.exe reads Alt with its other keys (the table at
0x47fac0, polled at 0x4562f0) but nothing in the game uses it, and the
name of an item shows only under the pointer (0x40ee70). X stays the
manual's special items window.

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

## When the hero falls

At no health the hero drops where they stand, as in the original
(0x435b60): the death sound, the parting glow (option 11000023) and the
death chart (4, facing nowhere) when their figure has one. An Elixir in the
bag brings them straight back instead. A fallen hero takes no more blows.

Once the last frame has been held for 120 of the original's ticks, the
original takes half the hero's gold and wakes them in the area's town
(scenario + 0x60, at its entry point + 0x64; 0x426200) with full health and
mana. Ours opens a Game Over window there instead (`gameOverWindow`):
CONTINUE loads the hero's last save, and EXIT goes back to the title
screen. A hero with no save yet wakes in town as in the original. Original
mode does what the original does. The code is in `src/game/game_over.cpp`.

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
| Autosave in Town | ours: ON (the default) saves the game, with "Autosaved" on screen, whenever the hero comes into a town from another area: on foot, by warp gate or by Transport. It saves into the hero's own slot (the one last saved or loaded), or the first free one if the hero has never saved. A town is an area's own, the scenario Transport leads to (scenario + 0x60), so the Caravan, Sena's Small Shrine and the Tower of Ordeal's rest floors count too. The original has no autosave, so original mode turns it off and dims the row. Saved in options.txt. |
| Art | ours: ORIGINAL or OURS, the art the original game is drawn with; F12 swaps it too. Only shown when playing with the original files. |
| Mission List | opens the mission list |
| Map | opens the area map, as N does |
| Help | the keyboard help window |
| Save and Return to Title | saves, then shows the title menu |
| Save and Exit | saves, then quits |
| Exit | ours: quits without saving. Not on the original's menu, so original mode leaves it out. |

### Debug tab

Ours, not the original's: debug options and cheats. Tab (or Page Up/Page
Down, or a click on the tab names at the top) switches between the settings
and this tab. Original mode turns every cheat off and dims its row.

| Row | What it does here |
|---|---|
| Experience Rate | multiplies the hero's experience from kills and quest rewards, spell training (each use counts that many times), and the companion's experience (each kill counts that many points): 1x, 2x, 5x, 10x or 20x, 1x by default. A cheat. Saved as `xp_rate` in `options.txt`. The original goes up one level at a time and drops whatever is over; above 1x the extra carries over instead, through as many levels as it fills, so a 100% quest reward at 2x is two levels. |
| Luck | multiplies the drop weight of unique (gold-named) items wherever a monster's drop (or a script's drop list) picks an item by weight: 1x, 2x, 5x, 10x or 20x, 1x by default. A cheat. With the original files about 2% of dropped equipment is unique at 1x, 4% at 2x, 8% at 5x, 13% at 10x and 23% at 20x; magic items are already about 88% at 1x, so luck leaves them alone. Above 1x it also lets uniques up to the hero's level drop where the drop list's highest item level is lower; the first areas' lists stop at item level 1-3 and the lowest unique is level 4, so without this no unique could drop there (with the original files, a level 5 hero at 20x finds one in about 40 ordinary kills there). It only changes which item comes, not how often something drops, and never gives a unique from a pick the drop tables keep to normal and magic items. Items a drop list names outright (potions, mines, quest items) are not affected, and the open pack has no unique items yet. Luck also rolls the modifiers of dropped items (magic and unique) nearer the top of their ranges: each value is the best of 1, 2, 5, 10 or 20 rolls, so on average it lands 50%, 67%, 83%, 91% or 95% of the way up (in the original data, mostly attack, defense, hit, evasion and their magic forms, and attack and walk speed). Shop stock and items placed on maps roll as usual. Saved as `luck` in `options.txt`. |
| Gold Rate | multiplies the gold monsters drop and the gold scripts put out of objects (chests, pots, barrels and so on): 1x, 2x, 5x, 10x, 20x or 100x, 1x by default. A cheat. Gold that people hand over as a reward, shop prices and selling are not changed. Gold over 10,000 lands in more piles of at most 10,000 around the first, as the bag's gold stacks hold no more; one drop gives at most 10,000,000. Saved as `gold_rate` in `options.txt`. |
| Unlock All Gates | marks every warp gate found (its `WARP` flag) in the game being played, so the GATE window lists them all, with the original files or the open pack. A cheat, for testing; only in a game, and dimmed once every gate is found. It changes the save's flags, not `options.txt`. The same as the `--warp-flag all` test flag. |
| Unlock All Storage | opens every page of the Giant Warehouse (tabs III to X; its `EXSTRG` flags) in the game being played, with the original files or the open pack. The original opens them one by one as quest rewards (see [formats.md](formats.md#weapon-sets-the-giant-warehouse-and-the-gold-split-box)); the warehouse itself is still on the Tower of Ordeal's 12th floor. A cheat, for testing; only in a game, dimmed once every page is open, and off in original mode. It changes the save's flags, not `options.txt`. |
| Unlock All Magic | teaches, at level 1, every spell the hero's classes learn at level-ups (tables 104-107): the mercenary's (Transport, Plasma), the current class's and any class the hero was before. Spells already known keep their level and use count; new ones fill free places on the F1-F8 bar. Other classes' spells (a warrior gets no Moon or Dread Deathscythe) and spells only scripts teach (Identify) are left out, since the original never gives them to that class. A cheat, for testing; only in a game, dimmed once every such spell is known, and off in original mode. Saved with the game (its spell book), not in `options.txt`. |
| No Clip | ON lets the hero walk through walls, trees, buildings, closed doors and chests: anything the map's walkability grid blocks or a blocking object stands on. The hero still stays on the map's grid. A cheat, for testing; off by default and off in original mode. Saved as `no_clip` in `options.txt`. Turned off (or a game loaded) with the hero inside a wall, they still walk freely until they are back on open ground, so they can walk out. |
| Invincible Companion | ON means nothing hurts the companion: monster blows, monster spells and traps still hit (and show), but take no health, stun or make it flinch, so it never goes down. A cheat, for testing; off by default and off in original mode. Saved as `invincible_companion` in `options.txt`. |
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
