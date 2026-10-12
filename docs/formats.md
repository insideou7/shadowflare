# ShadowFlare file formats

Notes from reverse engineering the Windows release. Everything here was
worked out from the game's own DLLs, which export C++ class and method
names (for example `RKC_UPDIB_UPD::Read`), so each section names the
function it comes from. All integers are little-endian.

Python reference decoders live in `tools/re/`; the engine's C++ readers
are in `src/formats/`.

## Install layout

| Folder | Contents |
| --- | --- |
| `Map/` | `<name>.map`, plus `Ground/<name>.Gnd`, `Object/<name>.Obl`, `Pattern/<name>.Lst` and the sprite banks they list |
| `Character/` | `ENEMY`, `PEOPLE`, `PARTNER`, `ITEM`, `OBJECT`, `OPTION`: one folder per character with `Animation.Njp/.Sdw/.Caf` |
| `Player/` | Male/Female player animations and shared effects |
| `Scenario/` | Per-quest scripts (`.Scs`), map control data (`.Mct`) and sprites |
| `System/` | UI graphics, fonts, music and voice (`.Voc`), parameter tables |
| `RKC_*.dll`, `RK_FUNCTION.dll` | Engine libraries; the format readers below come from these |

Every data file starts with a 16-byte text signature, such as
`NJudgeUniPat003` or `RPGSCRN_GNDv000`.

## RCLIB-L compression (`RK_LzDecodeMemoryToMemory`, RK_FUNCTION.dll)

Used inside sprite banks, ground files and tables.

```
char  magic[8]      "RCLIB-L\x1a"
u32   unpackedSize
u32   packedSize
u8    data[packedSize]
```

The payload is LZSS:

- 4096-byte ring buffer, zero-filled, write position starts at `0xFEE`.
- Each flag byte covers eight items, **most significant bit first**.
- A set bit is a back reference: two bytes `lo, hi`; position = `lo | (hi & 0xF0) << 4`, length = `(hi & 0x0F) + 3`.
- A clear bit is one literal byte.

`RCLIB-H` (Huffman) also exists in the DLL but has not been seen in data yet.

## Sprite banks: `.njp` and `.sdw` (`RKC_UPDIB_UPD::Read`, RKC_UPDIB.dll)

Signature is one of `NJudgeUniPat`, `UnitePatData` (adds collision data
per pattern) or `ShadowLowPat` (1-bit shadow masks), followed by a
three-digit version (0 to 3).

```
char magic[16]
u32  partCount
if version >= 3: u32 totalPartBytes
part[partCount]:
    u32 bpp            4 or 8 (ignored for ShadowLowPat, which is always 1)
    u32 width, height
    u32 compressed     1 = an RCLIB-L block follows, 0 = raw pixels
    pixels             DIB layout: rows bottom-up, each padded to 4 bytes
u32  patternCount
if version >= 3: u32 totalListEntries
pattern[patternCount]:
    u32 listCount
    i32 x, y, width, height    bounds; all zero in character banks
    if UnitePatData: u8 judge[0xA8]
    if version >= 1: i32 palette
    entry[listCount]:          28 bytes
        i32 flags, part, x, y, unknown, scaleX, scaleY   (scales are per mille)
u32  paletteCount
palette[paletteCount]: 256 x (u8 r, g, b, pad)
if version >= 2 and data remains: per pattern, u32 length + name bytes (Shift-JIS)
```

A pattern is drawn by placing each listed part at `(x, y)` relative to
the object's anchor. Palette index 0 is transparent except on ground chips.
In character banks the first half of the patterns are empty placeholders.

## Maps

A map `<name>` is made of four files. `Pattern/<name>.Lst` is a text list
of sprite bank file names, one per line, with `?` for unused slots. The
other files refer to banks by their line index.

### Ground: `.Gnd` (`RKC_RPGSCRN_GROUNDBLOCK::ReadFile`, RKC_RPGSCRN.dll)

```
char magic[16]   "RPGSCRN_GNDv000"
i32  width, height          in chips
i32  chipWidth, chipHeight  64 x 48
i32  magX, magY             160, 160
u8   compressed, then RCLIB-L block (or raw) of width*height*6 bytes:
     three planes of i16, one after another:
       plane 0: flags (always 0 so far)
       plane 1: bank index into the .Lst (-1 = empty)
       plane 2: pattern within that bank
u8   compressed, then RCLIB-L block (or raw) of judgeWidth*judgeHeight i16:
     the walkability ("area judge") grid, row-major
```

Chip `(cx, cy)` is drawn at screen pixel `(cx * 64, cy * 48)`.

The judge grid is laid out in world space, not chips: cell `(jx, jy)`
covers world `x` in `[jx*magX, (jx+1)*magX)` and the same for `y`, so on
screen each cell is a diamond. Its size is not stored; it is derived from
the map size (`RKC_RPGSCRN_GROUNDBLOCK::CalcAreaJudgeFromMap`):

```
CalcGroundPos(sx, sy):        map pixel -> judge cell
    p1 = 15*magX/100, p2 = 10*magY/100
    jx = floor((p1*sy + p2*sx) / (2*p1*p2))
    jy = floor((p1*sy - p2*sx) / (2*p1*p2))
take the four map corners (0,0) (W,0) (0,H) (W,H) in pixels:
    judgeX = jx(0,0) - 1           judgeWidth  = jx(W,H) - judgeX + 1
    judgeY = jy(W,0) - 1           judgeHeight = jy(0,H) - judgeY + 1
```

This matches the stored data exactly for all 165 maps. Values seen:

| bits | meaning |
|---|---|
| 0x0001 | blocked (walls, tree trunks, everything outside the map) |
| 0x0002 | unknown, rare (a few maps) |
| 0x8000 | unknown; set on some cells near map edges, often together with 0x0001. ShadowFlare.exe does not seem to test it; exits are script triggers instead (see `Scenario.Scs`) |

### Objects: `.Obl` (`RKC_RPGSCRN_OBJECTBLOCK::ReadFile`)

```
char magic[16]   "RPGSCRN_OBJv001"
i32  count
object[count]:               42 bytes in v001
    i32 x, y                 world coordinates
    i16 bank, pattern, status, trans, height, palette
    if version >= 1: i16 r, g, b   tint, per mille
    i32 judge[4]             collision box
```

### World to screen (`RKC_RPGSCRN::CalcRealPos`)

```
screenX = (worldX - worldY) * 15 / 100
screenY = (worldX + worldY) * 10 / 100
```

15 and 10 are the defaults set in the `RKC_RPGSCRN` constructor. The
result shares its origin with the ground chip grid.

## Character animation: `.Caf` (`RKC_RPGSCRN_CHARANIM::ReadCafFile`)

Each character folder has `Animation.Caf` (charts), `Animation.Njp`
(body sprites) and `Animation.Sdw` (shadows). The player uses
`Animation00.*`.

```
char magic[16]   "CHRAnimation002"
u32  chartCount            one chart per action: 0 idle, 1 walk, then attacks, hits, death...
chart[chartCount]:
    i16 status             1 on looping actions
    direction[9]:          0 = south, then counter-clockwise on screen
                           (south-east, east, ...); slot 8 is empty
        u32 layerCount
        i16 frameCount
        layer[layerCount]:
            u32 frames
            cell[frames]:
                i16 status     8 = shadow, pattern is in the .Sdw bank
                i16 trans      per mille
                pattern        i16 if version <= 1, else i32
                i16 priority   draw order, high first: 0 is in front
if version >= 1: i32, i32 (unknown)
```

Body and shadow patterns share one numbering. The `.Njp`'s first
patterns are empty placeholders for the shadow slots. Monsters have two
layers (shadow and body). The player has 23: shadow, body, then one per
equipment slot. A pattern is drawn with its anchor at the character's
feet.

Chart numbers, worked out by rendering each chart:

| chart | monsters (`Character/ENEMY`) | player (`Player/Male`) |
|---|---|---|
| 0 | idle | idle |
| 1 | walk | walk |
| 2 | hit | run |
| 3 | death | hit |
| 4 | attack | ? (44 frames; its patterns are empty in `Animation00.Njp`) |
| 5 | unused | bare-handed attack |
| 7, 8 | special attacks (some monsters) | other attacks, not identified |

Townspeople have three charts: idle, walk and one more.

## Scenarios: `Scenario/<id>/Scenario.Mct`

Each scenario folder (`Scenario.Mct`, `.Scs` script, `.Njp` portraits) is
one place at one point in the story. Several can share a map. Read by
ShadowFlare.exe at 0x427b50. All 209 files parse with 12 bytes left over.

```
char magic[16]          "MCED DATA v0000\x1a"
char aidPath[0x104]     AI data, usually System\Game\Parameter\Control.aid
char mapPath[0x104]     e.g. Map\c02_00.map
i32  unknown[3]
char title[0x100]       area name, e.g. "Kanfore, Mining Town"
3x { i32 n; i32 kinds[n] }   character types used: objects, people, enemies
4 groups (objects, people, enemies, items), each { i32 n; Record[n] }
i32  n; EntryPoint[n]   where the party appears: i32 id, x, y, direction
12 more bytes, unknown
```

Record (same layout in every group; only the trailing block size differs):

```
i32  id                 plus 10,000,000 (objects), 12,000,000 (people),
                        20,000,000 (enemies) or 30,000,000 (items) gives
                        the in-game object id
i32  kind               Character/<OBJECT|PEOPLE|ENEMY>/<kind as %08d>;
                        people with kind >= 1,000,000 are PARTNER/<kind-1000000>;
                        -1 = no sprite (spawners, trigger areas)
i32  nameLength; if > 0: char name[nameLength], u32 nameColour (0x00BBGGRR)
i32  unknown
i32  x, y               world position
i32  rect[4]            left, top, right, bottom around (x, y)
i32  direction          0 = south, counter-clockwise as in .caf; 8 = none
i32  n; i32 unknown[n]
i32  hasTable; if 1: i32 m; i32 a[m]; i16 b[m], c[m], d[m]
i32  unknown
u8   extra[0x34 | 0x2c | 0x13c | 0x10]   per group: objects, people, enemies, items
```

The enemy block (0x13c bytes) holds the monster's stats, read as i32
words. The loader (0x428bc2-0x428f02) copies the words, out of order, into
the monster object from + 0xc4 on, and the combat code reads them there:
0x459690 hands the damage code (0x412a40) + 0xe8 as defence, + 0xf0 as
magic defence and + 0xd8 as element; the attack code (0x45a749) reads the
attack and hit of the chosen slot at + 0xf8 and + 0x104; the hit test
(0x45eb2b) takes evasion from + 0xec, or + 0xf4 for spells; the spell
code (0x45b043) reads + 0x134 to + 0x158.

| word | monster + | meaning |
|---|---|---|
| 1-4 | 0xc4 | area it wanders in: left, top, right, bottom around its position |
| 6 | 0xd8 | element: 0 fire, 1 water, 2 earth, 3 thunder, 4 holy, 5 dark, 6 gel, 7 metal |
| 7 | 0xdc | unknown: 1 for almost every monster, 100 for 19 kinds (perhaps bosses); Counter Burst reflects half as much onto those |
| 8 | 0xe4 | hit points |
| 9 | 0xe8 | defence |
| 10 | 0xec | evasion (50 for almost all) |
| 11 | 0xf0 | magic defence |
| 12 | 0xf4 | magic evasion |
| 13 | 0x17c | experience given |
| 14 | 0x180 | level |
| 15-22 | | name of its behaviour list in `Control.aid` (Shift-JIS, NUL-terminated) |
| 23-25 | 0xf8 | attack of each of its three attacks (Control.aid actions 2-4) |
| 29-31 | 0x104 | hit of each attack |
| 32-34 | 0x134 | its three spells (actions 5-7), as the hero's spell numbers; -1 none |
| 35-37 | 0x14c | each spell's power |
| 38-40 | 0x140 | the level whose Table.Tbd columns each spell uses (hit, element) |
| 41-43 | 0x158 | added to each spell's hit |
| 49 | 0x198 | chance of dropping gold, percent (usually 10; read at 0x45a030) |
| 50, 51 | 0x19c | gold dropped, min and max |
| 77 | | movement speed, per mille (usually 1000) |

Words 26-28, 44-48, 52-75 and 78 are copied too but their use is not known
yet. Before this was read from the exe, the engine took word 9 as the
attack and 10-12 as hit, defence and evasion, which made monsters far too
weak.

The item block (0x10 bytes) is the item's class and code in Item.Ibn
(see Items below) and two zeros.

Object kinds have only `Pattern.Njp`/`.Sdw`, no animation file.

Entry point ids come in fours, one per party member: entering a
scenario at entry `e` (see `CHANGESCENARIO` below) puts the player at the
point with id `e * 4` and the others at `e * 4 + 1` to `+ 3`. Towns also
have entries 50 and 100 (ids 200 and 400), probably for warp gates.

## Scenario scripts: `Scenario/<id>/Scenario.Scs` (`RKC_RPG_SCRIPT::ReadBinary`)

The event script for a scenario, compiled from a text language whose
keywords are still in RKC_RPG_SCRIPT.dll (`CHANGESCENARIO %s %s`,
`TALK`, `IF`, `[ONJUDGE ...]`...). All 209 files parse.

```
char magic[16]   "ScenaScriptV000"
i32  n; { i32 id, value }[n]            TEMP flags and their starting values
i32  n; { i32 id, value }[n]            network flags
i32  n; { i32 id; i32 len; u8 text[len] }[n]   messages, each byte bitwise inverted
i32  n; Trigger[n]: i32 network, event, character, sentence
i32  n; Sentence[n]: i32 commands; Command[commands]:
         i32 opcode; i32 operands; { i32 type, value }[operands]
```

A trigger runs a sentence when `event` happens to `character` (an
in-game id, placement id plus the group offset above). Event 3 is
`ONJUDGE`: the player walks into the character's rectangle. Map exits
are invisible objects with a rectangle and an `ONJUDGE` trigger whose
sentence runs `CHANGESCENARIO scenario entry`; 462 of them lead to a
real entry point with a fresh save. The scenario number is the folder
name (`Scenario/%08d`); scenario 2 is referenced but missing, probably
cut content.

Operand types (`AnalyzeOPERAND`): 0 operator or sentence number, 1
constant, 2 `MSG`, 3 `ELIV`, 4 `TEMP`, 5 `NET`, 6 `CPOSX`, 7 `CPOSY`, 8
`PLAYMODE`, 9 `CCHECK`, 10 `WARP`, 11 `SAVE`, 12 `QUEST`, 13 `EXSTRG`.
`IF` operators (`AnalyzeOperator`): 0 `=`, 1 `!`, 2 `>`, 3 `<`.

Operand values (ShadowFlare.exe 0x4346b0): types 0-2 are the value
itself; `TEMP`, `NET`, `WARP`, `SAVE` and `QUEST` are variables; `CPOSX`
and `CPOSY` are a character's world position; `PLAYMODE` is single or
network play.

Command numbers come from the keyword table in RKC_RPG_SCRIPT.dll (each
keyword stores its number and operand count). ShadowFlare.exe runs them in
a switch at 0x430ffe (jump table 0x4344f0):

| # | Command | # | Command | # | Command |
|---|---------|---|---------|---|---------|
| 0 | `IF a op b sentence` | 26 | `MESSAGEBOXVALUE char dx dy value r g b` | 52 | `GETREPAIRPRICE slot var` |
| 1 | `SETFLAG var value` | 27 | `MESSAGEBOX char dx dy msg r g b time` | 53 | `GETTOTALGOLD var` |
| 2 | `TALK msg answer mode default speaker height` | 28 | `ROUTIN` | 54 | `PAYGOLD amount` |
| 3 | `TALKPARTNERSTATUS kind answer` | 29 | `ROUTINNET` | 55 | `CHECKUNOPINIONITEM var` |
| 4 | `IDENTIFY` | 30 | `CREATEOPTIONANGLE` | 56 | `ABSOLUTEOBJECTFLAG` |
| 5 | `TRADE shop` | 31 | `CHECKENEMYLIVE` | 57 | `GETPLAYERSEX var` |
| 6 | `INITTRADE shop list` | 32 | `CHECKENEMYDEAD` | 58 | `CHECKITEMEXIST` |
| 7 | `HEALLIFE` | 33 | `GETNEARPLAYERPOS` | 59 | `DELETEITEM` |
| 8 | `HEALMENTAL` | 34 | `CALCMYPLAYERDIST char var` | 60 | `SETUNLOCKSW` |
| 9 | `REPAIR slot` | 35 | `CALCANGLE` | 61 | `GETPLAYERLEVEL var` |
| 10 | `CREATEITEM class code x y min max` | 36 | `CREATEEFFECT` | 62 | `SETQUESTFLAG` |
| 11-15 | `ADD`, `SUB`, `MUL`, `DIV`, `REM var value` | 37 | `WARPGATE` | 63 | `GETPARTNERLIFE life max` |
| 16 | `PLAYSOUND sound ? x y` | 38 | `DELETEWARPGATE` | 64 | `STARTVISUAL` |
| 17 | `CHANGESCENARIO scenario entry` | 39 | `GETRANDAM min max var` | 65 | `SETTINGRAIN` |
| 18 | `C_STOP char` | 40 | `CREATEEFFECTCHAR` | 66 | `GETMYPLAYERNO` |
| 19 | `C_MOVABLE char` | 41 | `STORAGE 0` | 67 | `MEMORIZEMAGIC` |
| 20 | `C_ACTIONREQ` | 42 | `GETPLAYERLIFE` | 68 | `GETEXPERIENCE` |
| 21 | `C_SETANGLETO char target` (0 = player) | 43 | `GETPLAYERMENTAL` | 69 | `CHECKMEMORIZEDMAGIC` |
| 22 | `SETACTIVE char` | 44 | `GETPARTNERKIND var` | 70 | `ENABLECLASSCHANGE class` |
| 23 | `SETINACTIVE char` | 45 | `CHANGEPARTNER kind` | 71 | `GETCLASS var` |
| 24 | `CREATEITEMTABLE` | 46 | `SETCHARTRANS` | 72 | `CHANGECOLOR` |
| 25 | `RESSURECT` | 47 | `GETCHARTRANS` | 73 | `STARTCARDGAME` |
| | | 48 | `DRAWQUESTNAME quest` | 74 | `GETCARDGAMERESULT` |
| | | 49 | `SETCOMPASSES msg` | 75 | `SETITEMDIRECT` |
| | | 50 | `GETENTRYPOINT var` | | |
| | | 51 | `SETMSGDEFINE v0 ... v19` | | |

`MESSAGEBOX` floats a message at (dx, dy) screen pixels from a character
or map object, such as an area's name over its warp gate (Remote Town runs
it every frame near the gate); `MESSAGEBOXVALUE` does the same with a
number. The colour is red, green, blue; `time` is 1000 where it appears,
taken as a second. `STORAGE` opens the warehouse, the shop window's twin
(Status.njp patterns 14 and 15, a 9 x 10 grid); Remote Town runs it when
the player uses the Warehouse object (10000300).

`IDENTIFY` identifies everything the player wears and carries;
`CHECKUNOPINIONITEM` sets its variable to 1 when anything is still
unidentified, else 0. `REPAIR` and `GETREPAIRPRICE` take a part: 0 the
weapon, 1 helmet, 2 body armour, 3 shield, 4 boots, -1 every weapon and
armour in the bag (GETREPAIRPRICE prices the bag for any number above 4
too; REPAIR then does nothing). Neither takes gold: the script pays with
`PAYGOLD`. Remote Town's blacksmith (12000001) uses them all, with the
identify price in TEMP 1000017.

The partner commands (3, 44, 45, 63) are described in
[partners.md](partners.md).

`TALK` shows a message over the speaker (-1 = the character whose trigger
is running), filled in with sprintf from the values `SETMSGDEFINE` set.
Text between `~` marks is an option to choose; `default` is the option
picked to begin with. Scripts carry on straight away; when the box closes
the chosen option (from 0) goes into `answer` and the speaker's event 1
triggers run. `CREATEITEM` with class 4 code 0 drops a random amount of
gold between `min` and `max`, in piles of at most 10,000.
`ENABLECLASSCHANGE` 1, 2 and 3 offer the hero the warrior, hunter or
wizard (player +0x30 = 6, 5 or 9, 0x4341da); the hero takes it in the
status window (see Classes below). `GETCLASS` reports the class on offer
the same way, 0 for the mercenary (+0x30 = 16, 0x434186). Only the
instructors in Scenario 03900003 use them.

Trigger events (0x4304b0-0x430a40): 0 the player speaks to the character,
1 its message box closed, 3 `ONJUDGE`, 4 an enemy died (inferred from the
caller), 5 every frame, 7 the scenario was entered.

Shops: `INITTRADE shop list` stocks shop `shop` from row `list` of
Table.Tbd table 32, whose cells are rows of table 33 (0x430c10). A table
33 row is: class (-1 = weapon, armour or accessory), highest item level,
lowest item level, item code (-1 = pick one), count (-1 = random 0-29),
two unused, then whether normal, magic and unique items may be picked.
Picks are weighted by the item's drop weight among items in the level
range with a non-zero drop pool. Goods fill a 9 x 10 grid row by row.
A shop sells at the item's price and buys at a quarter of it (0x4661c0).

## Monster behaviour: `System/Game/Parameter/Control.aid` (`RKC_RPG_AICONTROL::ReadFile`)

```
char magic[16]   "RKC_AIDATA v001\x1a"
i32  listCount, eventCount       64 lists, 18 events
list[listCount]:
    i32 nameLength; char name[nameLength]   e.g. "goblin normal attack" in Japanese
    if version >= 1: i32 walkSpeed
    event[eventCount]: i32 n; entry[n]:
        i32 action
        i32 param[9]     [0] priority, [1] duration in ticks, [2] weight,
                         [3] movement speed, [4], [5] wander pause and random extra
        i32 cond[6]      [0] = 1: own health within [1]..[2] percent;
                         [3] = 1: a target within [4]..[5] world units (-1 = open)
```

Every monster names its list in its scenario record. How ShadowFlare.exe
runs it (selection at 0x45c9f0, actions dispatched at 0x459340): each
tick the monster's latest event picks an action from that event's
entries. Entries whose conditions hold are collected, a higher `param[0]`
throws away what was collected before it, and one is drawn at random
weighted by `param[2]`. Events 1-10, 16 and 17 with no matching entry fall
back to event 0. Distances are gaps between the two collision boxes.

| event | when |
|---|---|
| 0 | an action finished |
| 1, 9, 10 | wander, retreat or approach finished |
| 11, 12, 14, 15 | every tick while waiting, wandering, retreating or approaching |
| 16 | an attack or spell animation finished |
| 17 | hit |
| 2-7 | used by some lists, not seen being raised yet |

| action | does |
|---|---|
| 0 | wait for `param[1]` ticks |
| 1 | wander inside the home area |
| 2-4 | melee attacks |
| 5-7 | spells |
| 8 | stop |
| 9 | move away from the target |
| 10 | move towards the target |
| 11 | walk home |

Speeds are multiplied by the monster's speed (word 77) / 1000.

## Tables: `System/Game/Parameter/Table.Tbd` (`RKC_RPG_TABLE::ReadBinaryFile`)

```
char magic[16]   "TABLE DATA V000\x1a"
i32  packed      non-zero: an RCLIB block follows; 0: i32 size, then raw bytes
body:
    i32 tableCount
    table[tableCount]:
        i32 number, rows, cols
        i32 values[rows * cols]
        string[rows * cols]: i32 length, then length bytes, each bitwise inverted
```

138 tables. Identified so far: 40 area names, 41 quest titles,
60 partner names, 600-621 spell descriptions and 700-747 quest
descriptions. The numeric tables found so far:

- 13: experience per level, one column, 100 rows. Row `level - 1` is what
  that level takes; the count starts again from 0 on a level up and any
  surplus is lost (0x43430f, and the bar at 0x404643). Closed by 999999999
  and 0, so the top level is 99.
- 900-907: the hero's stats, 13 rows by one column per level in a class
  (level-up function 0x412fb0, which reads table
  `901 + 2 * map[class] - [character+0x28]`). 900/901 hold the starting
  stats in column 0 and four classless levels (900 looks like the male
  hero: more health and attack); 902/903 the warrior, 904/905 the
  wizard, 906/907 the hunter. Each level-up first stores the class held
  in a byte per level (+0xec + level) and then counts that class's bytes:
  the n-th level gained in a class reads column n - 1. A new hero's byte 0
  is the mercenary (16), so mercenary levels read 900 from column 1. The
  rows, from the stats function 0x44ea60 that adds equipment to them: 0
  attack speed, 1 walk speed, 2 health, 3 magic, 4 strength (the manual's
  status window shows 180 at level 5: 140 + 4 x 10),
  5 attack, 6 defence, 7 magic attack, 8 magic defence, 9 hit, 10 evasion,
  11 magic hit, 12 magic evasion.

Combat between the player and a monster: the chance to hit is hit minus
evasion, kept between 20 and 98 percent (0x413e00); damage is attack x
(90, 100 or 110)% minus defence, at least 1 (0x412a40), scaled by the
elements through tables 7 and 11. See [magic.md](magic.md) for elements,
spells and the tables they use.

## Classes (ShadowFlare.exe)

The player keeps a job number at +0x2c and the job on offer at +0x30: 16
mercenary, 6 warrior, 5 hunter, 9 wizard (the names at 0x405750 are in a
table of 17 per sex, witch for the heroine; jobs 2, 8 and 10 have stats
slots but no names or tables). Every hero starts as a mercenary.

- Kills (0x4134a0): when the player's own blow kills a monster, +0xc0
  counts it, and +0xc4[n] counts it by the weapon in hand: Item.Ibn weapon
  subtype n for 0-7, slot 8 for staffs, rods (subtypes 8, 9) and spells.
  Bare hands only add to the total. Both reset on every level-up.
- Level-up (end of 0x412fb0, from level 5): each slot as a percentage of
  the total. If the hero is not a wizard and magic is at least 20%, the
  wizard is offered; else if not a hunter and slot 5 (bows) is at least
  30%, the hunter; else a non-warrior with slots 0 + 1 + 3 (swords, axes
  and maces) at least 50% is offered the warrior; otherwise the offer goes
  back to the hero's own job. On reaching level 5 the job and the offer both
  become the warrior, whatever was counted.
- While the offer differs from the job, the level-up window says "It is
  possible to change your occupation to [...]" (at level 5: "Changed your
  occupation to [...]", 0x451771), and the status window (Status.njp
  pattern 5) has a Change Career button: a click between (17, 64) and
  (112, 79) copies +0x30 to +0x2c and works the stats out again (0x4474b0).

## Items: `System/Game/Parameter/Item.Ibn` (ShadowFlare.exe, loader 0x462f90)

```
char magic[16]   "SFItemDataV0000\x1a"
i32  checksum    sum of the unpacked bytes as signed chars (checked at 0x463bd1)
i32  compressed  1
RCLIB-L header   magic, u32 unpacked, u32 packed
u8   packed[]    each byte first replaced through a 256-byte table, then LZSS
```

The substitution table is not in any data file: the exe builds it on the
stack with one `mov byte [esp+n], value` per entry between 0x463031 and
0x46376e (one entry is written from `bl`, which is 0, and a `push ebx;
push imm32` in the middle moves esp by 8), and applies it at 0x463ae6.
The engine decodes those instructions from the player's own
`ShadowFlare.exe` at run time (`itemSubstitutionTable` in
`src/formats/item_data.cpp`); without the exe, items fall back to the
open content pack's `items.txt`. The checksum confirms the result.

The unpacked data holds five classes in order, each an `i32 count` and its
records: 0 weapons (1264), 1 armour (1281), 2 accessories (239),
3 consumables (31) and 4 special items such as gold and quest items (45).
An item is identified by (class, code); scenario files place items that way.

A record is `i32 length, name` and `i32 length, unidentified name` (bytes
bitwise inverted) followed by i32 fields. The shared part, in file order
(object offset in brackets):

| field | meaning |
|---|---|
| subtype (0x10) | weapon: 0 one-handed sword, 1 axe/mace, 3 two-handed sword, 5 bowgun/sling, 8 staff, 9 rod; armour: 0 helmet, 1 body, 2 shield, 3 boots |
| code (0x14) | id within the class |
| quality (0x18) | 0 normal, 1 magic, 2 unique, 3 quest |
| drop pool (0x88), drop weight (0x78) | used by the drop tables |
| price (0x40), fixed price (0x44) | |
| width, height (0x20, 0x24) | size in 32-pixel inventory cells |
| weight (0x28) | |
| inventory bank, pattern (0x2c, 0x30) | `System/Game/Pattern/Item%04d.njp` |
| ground folder, picture (0x34, 0x38) | `Character/ITEM/%08d`: a chart of its `Animation.Caf` (folders 0-6) or a pattern of its `Pattern.njp` (7-11) |
| icon parameter (0x3c), ground tint R, G, B (0x50-0x58) | |
| glow layer (0x68), its tint (0x6c-0x74) | a second hero layer for some weapons, or -1 |
| 0x7c, 0x80[2] | unknown |

Then per class: weapons have durability, ten stats (attack, hit, defense,
evasion, magic attack, magic hit, magic defense, magic evasion, attack
speed, walking speed), item level, required level, an unknown, the weapon
effect tint, the hero layer and its tint, six projectile and flag words
(the last is "two-handed"), 8 elements, 39 modifiers of {min, max,
chance%} and 8 elemental modifiers of 3. Armour has durability, stats,
item level, required level, hero layer and tint, elements and modifiers.
Accessories have item level, required level and modifiers. Consumables
have item level and nine effect words (word 0: may go in the belt; 1:
health restored; 2: mental power restored). Special items have nothing
more.

Each item the game makes keeps its own durability left (+0xbc, out of
+0xb8 from the record) and an identified flag (+0x1c). Magic and unique
items (quality 1 and 2) are made unidentified and show the second name
until then (0x462d30). Each blow the hero aims wears the weapon one point
30% of the time; each blow that reaches the hero wears the helmet and
boots 20% and the body armour and shield 30% of the time (not a shield
behind a two-handed weapon). An item at 0 is broken and adds nothing to
the hero's stats (0x44eb5b). Mending one costs nothing when it is unworn,
else max(1, worn points x (price / 10) / durability) (0x4667a0). The
original adds its modifiers' surcharges to the price first; the engine
does not read modifiers yet.

Axes and two-handed swords (subtypes 1 and 3) or the two-handed flag hide
the shield. Worn weapons, body armour and shields turn on their hero
layer in `Player/Male/Animation00.Caf` (0 body and 1 shadow are always
on): 5-8 armour, 9-11 shields, 12-21 weapons. Helmets, boots and
accessories are not drawn.

## Not decoded yet

| Signature | Files | Notes |
| --- | --- | --- |
| `MAPED_FILE v000` | `.map` | 36 bytes; small header read by `RKC_RPGSCRN::ReadMapFile` |
| `VoiceData  V003` | `.Voc` | Music and voice (`RKC_DSOUND.dll`) |
| `ShadowFlare0005` | `Save/*.Ssv` | Save games |
