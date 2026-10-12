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

What a click picks when things overlap is set by Click Priority in the
Settings Menu, and how far from the cursor it reaches by Click Range.

## Keys

| Key | | In yet |
|---|---|---|
| F1-F8 | choose the spell for the right button | yes |
| F9 | choose the attack for the right button | yes |
| 1-8 | use a belt pocket item (1-4 the top row, 5-8 the bottom); a right click on it does too | yes |
| B | lay a mine where you stand; the MINE button by the belt does too ([mines.md](mines.md)) | yes |
| P | increased-power mode | no |
| Ctrl | run while held | yes |
| Shift | stop where you are; with a left click, attack in place | yes |
| Caps Lock | swap between the two equipment sets | no |
| Tab + click | companion: left click jumps (onto a monster, switch or spot), fetches an item, or shows its status; right click dashes; see [partners.md](partners.md) | yes |
| Space | companion active / inactive (or click its gauge at the bottom left); see [partners.md](partners.md) | yes |
| S | status window | yes |
| I | items window | yes |
| X | special items window | no |
| M | magic window | no |
| Q | mission list | no |
| N | map | no |
| R | switch walk / run | yes |
| H | help window | yes |
| Print Screen | screen shot, saved as a bitmap in `ScreenShot/` next to the saved game | yes |
| Esc | close the open window, or open the Settings Menu | yes |

Ours, not in the manual: F10 saves the game and F11 loads it; the arrow
keys walk; Page Up / Page Down switch maps, Home goes back to the start
point and J shows the walkability grid (for testing).

Changed from before: F9 used to load (now F11), N and P used to switch
maps (now Page Down / Page Up only), and WASD no longer walks (S is the
status window).

## Settings Menu

Esc during play opens it; Options on the title menu opens the same rows
without the save and exit ones. Up/Down choose a row, Left/Right or a click
change it, Enter steps through it or does it.

| Row | What it does here |
|---|---|
| Screen Mode at Start | WINDOW or FULL (full-screen desktop). Ours takes effect at once as well as at the next start. |
| Semi-transparent Player | ON draws a faint copy of the hero over walls and trees that hide them |
| Semi-transparent Shadow | OFF draws shadows solid black |
| Display Darkness | not in yet |
| Save Image at Game End | not in yet (no load-screen pictures) |
| Click Range | ON: a click reaches things in a square around the cursor; OFF: only right at it |
| Click Range size | MINI, SMAL, NORM, LARG, MAX. The original's sizes in pixels are not decoded yet; ours are 12, 20, 32, 44 and 60 pixels either side. |
| Click Priority | ENEM, ITEM, OBJ., PEOP, COMP in the order a click tries them; click one (or Left/Right) to bring it to the front |
| Eff. Volume, BGM Volume | not in yet (no sound) |
| Original Mode | ours: play by the original's rules, bugs included (see `src/logic/rules.h`). Off by default; new games use this, a saved game keeps its own. |
| Mission List, Map | not in yet |
| Help | the keyboard help window |
| Save and Return to Title | saves, then shows the title menu |
| Save and Exit | saves, then quits |

The options are kept in `options.txt` next to `save.txt` in SDL's settings
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
