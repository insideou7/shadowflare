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
ShadowFlare.exe at 0x427b50. All 209 files parse to their last byte.

```
char magic[16]          "MCED DATA v0000\x1a"
char aidPath[0x104]     AI data, usually System\Game\Parameter\Control.aid
char mapPath[0x104]     e.g. Map\c02_00.map
i32  town               the area's town (scenario + 0x60): 0 for Remote Town's
                        areas, 1000000 for Cold Svalt's, ...; where Transport leads
i32  unknown            scenario + 0x64
i32  areaKind          0 town, 1 field, 2 caravan, 3 boss room, 4 ruins/tower/fort,
                        5 cave, 6 shrine (scenario + 0x6c)
char title[0x100]       area name, e.g. "Kanfore, Mining Town"
3x { i32 n; i32 kinds[n] }   character types used: objects, people, enemies
4 groups (objects, people, enemies, items), each { i32 n; Record[n] }
i32  n; EntryPoint[n]   where the party appears: i32 id, x, y, direction
i32  unknown            scenario + 0x2c, use not traced
i32  transport          scenario + 0x70: 1 where Transport may be cast (fields,
                        dungeons), 0 in towns and the Caravan (0x441c00)
i32  light              how light the area is: 2000 lit (towns, fields), 1000
                        dark beyond the lights, 1200-1600 dim (see Darkness)
```

Record (same layout in every group; only the trailing block size differs):

```
i32  id                 plus 10,000,000 (objects), 12,000,000 (people),
                        14,000,000 (enemies) or 18,000,000 (items) gives
                        the in-game id scripts use (0x427b50)
i32  kind               Character/<OBJECT|PEOPLE|ENEMY>/<kind as %08d>;
                        people with kind >= 1,000,000 are PARTNER/<kind-1000000>;
                        -1 = no sprite (spawners, trigger areas)
i32  nameLength; if > 0: char name[nameLength], u32 nameColour (0x00BBGGRR)
i32  unknown
i32  x, y               world position
i32  rect[4]            left, top, right, bottom around (x, y)
i32  direction          0 = south, counter-clockwise as in .caf; 8 = none
i32  n; i32 states[n]   copied to the character at + 0x4c on: [0] shown
                        (there), [1] usable (clicked, talked to), [2] in
                        the way (objects: blocks movement); each scenario
                        load makes them NET flags 100,000,000 + id,
                        300,000,000 + id and 200,000,000 + id (0x4300e0)
i32  hasTable; if 1: i32 m; i32 a[m]; i16 b[m], c[m], d[m]
i32  light              the light it carries, Dark.njp part light + 1; -1 none
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
| 14 | 0x180 | drop list: the row of Table.Tbd table 30 rolled when it dies (0x45a000; see Drops) |
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

The object block (0x34 bytes, 13 words) is how a scenario object (a chest,
door, lever or gate; kind = folder `Character/OBJECT/<kind as 8 digits>`)
looks. The loader (0x428296) copies it to the object (vtable 0x476de8) at
+ 0xc4 on, and its draw function (0x45ddd0) reads it there:

| word | meaning |
|---|---|
| 0 | 1: a still picture, pattern word 1 of `Pattern.Njp` (shadow from `Pattern.Sdw`); else an animation, chart word 2 of `Animation.Caf`, drawn facing 8 (none), looping when the chart's status has bit 1, else holding its last frame |
| 3 | 1 sets flag 0x80 (use not traced) |
| 4 | height |
| 5 | 1: a partner's switch (see Partner dogs) |
| 6 | a linked enemy |
| 7 | flags; 4 draws it even at opacity 0 |
| 8 | opacity, per mille |
| 10-12 | red, green and blue, per mille |

Most objects come in sets at one place that the script swaps with
`SETACTIVE` and `SETINACTIVE`: a closed chest that is shown and usable
and an open one that is neither; a closed door and its two open halves;
a lever up and down. An object is drawn while its shown flag is set (a
missing flag counts as set, 0x4541c0), can be clicked while usable, and
stops the hero while in the way: the movement sweep (0x414990, through
`RKC_RPGSCRN_OBJECTBLOCK::SetJudgementObject`) keeps only those objects
as obstacles, using their rectangle. Pictures change only with these
states; an animation counts its frames only while shown.
`ABSOLUTEOBJECTFLAG id shown usable blocks` (case 56, 0x433a78) sets
the three for good (+ 0xfc = 1, + 0x100 on), so the object no longer
follows its NET flags. All 513 trigger areas (`ONJUDGE` objects with
no picture) are never in the way.

Object kinds 10, 11, 15 and 27 also have `Animation.Caf` (glowing globes
and the like); the rest have only `Pattern.Njp`/`.Sdw`.

Entry point ids come in fours, one per party member: entering a
scenario at entry `e` (see `CHANGESCENARIO` below) puts the player at the
point with id `e * 4` and the others at `e * 4 + 1` to `+ 3`. Towns also
have entries 50 and 100 (ids 200 and 400); entry 50 is where a warp gate
leads (see The GATE window).

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
network play (2 is network play); `ELIV n` is 1 while enemy 14,000,000 + n
is alive and 0 once it is dead; `CCHECK id` is 1 while the left button is held
on object `id`, which is shown and usable, and the hero stands within
reach of its rectangle (the "Brain Globe" a player holds down until its
countdown ends); `EXSTRG n` is a word list on the
player (+ 0x55c, use not traced).

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
`CREATEITEMTABLE list x y` drops what drop list `list` gives at (x, y),
as a dying monster does (see Drops).
`ENABLECLASSCHANGE` 1, 2 and 3 offer the hero the warrior, hunter or
wizard (player +0x30 = 6, 5 or 9, 0x4341da); the hero takes it in the
status window (see Classes below). `GETCLASS` reports the class on offer
the same way, 0 for the mercenary (+0x30 = 16, 0x434186). Only the
instructors in Scenario 03900003 use them.

Trigger events (0x4304b0-0x430a40): 0 the player speaks to the character,
1 its message box closed, 2 a NET flag changed (network play only:
0x430a40 runs it when another player's change arrives; the character is
the flag's number), 3 `ONJUDGE`, 4 an enemy died (inferred from the
caller), 5 every frame, 6 `ROUTIN` called it (the character is
ROUTIN's number, 0x4309f0), 7 the scenario was entered, 8 the card game
ended (0x40da90).

Activity flags: every placement's record carries three flags (see the
Record above). Each scenario load puts them in the NET flags, as
100,000,000 + id (whether the character or object is there),
300,000,000 + id and 200,000,000 + id, after the script's own NET flags
(0x4300e0). Out of network play the NET flags start again at every load,
like TEMP ones. `SETACTIVE id` sets all three to 1 and `SETINACTIVE id`
to 0; scripts also read and set them directly, such as a
lever's `NET[110040000]`.

`SETUNLOCKSW on` (case 60, 0x433edf) sets player + 0x159c, which the
player clears every frame (0x443490); while it is set the hero is drawn
with `Player/Common/UnlockSW` over them (animation 502 at 0x434ef0), the
glow while working a Brain Globe, so a script sets it on every frame it
wants it shown.

`CHECKENEMYLIVE first last var` puts in `var` the first enemy id in
first..last that is still alive, or -1 when none is; `CHECKENEMYDEAD`
the first that is dead. Every enemy placed in the scenario
counts as alive until killed, active or not (0x4300e0; a death clears it at 0x45bec0,
`RESSURECT` sets it again through 0x45a140).
`CHECKITEMEXIST class code var` sets `var` to 1 when the player has that
item anywhere: special items, the bag or worn.
`DELETEITEM class code` takes one away, looking in the same places.
`GETPLAYERLIFE hp max` and `GETPLAYERMENTAL mp max` read
the hero's. `GETEXPERIENCE percent` gives that share of the
experience the hero's level needs, which can raise one level.
`SETITEMDIRECT class code` hands the hero the item (kept with the
special items) unless they already have one.
All of these are cases of the script switch at 0x430ffe.

Shops: `INITTRADE shop list` stocks shop `shop` from row `list` of
Table.Tbd table 32, whose cells are rows of table 33 (0x430c10). A table
33 row is: class (-1 = weapon, armour or accessory), highest item level,
lowest item level, item code (-1 = pick one), count (-1 = random 0-29),
two unused, then whether normal, magic and unique items may be picked.
Picks are weighted by the item's drop weight among items in the level
range with a non-zero drop pool. Goods fill a 9 x 10 grid row by row.
A shop sells at the item's price and buys at a quarter of it (0x4661c0).

Drops: a dying monster drops gold first (0x45a030): `word 49` percent of
the time, an amount from words 50-51 raised by the Amount of Gold
modifiers worn (0x44f8b0; broken items count), at least 1, 100 units below
it. Then 0x45a000 rolls its drop list (Mct word 14) with 0x417550. A row of
Table.Tbd table 30 is: how many rolls (0 = one per party member), each
roll's chance in percent (checked as `rand % 100 <= chance`, so 15 drops
16 times in 100), then ten picks of (table 31 row, quality digits); each
roll that drops takes one pick with `rand % 10`. A row of -1 is nothing.
The digits allow normal (ones), magic (tens) and unique (hundreds) items:
111 any, 11 no uniques. Quest items always pass. A table 31 row is: class
(-1 = weapon, armour or accessory), highest and lowest item level (-1 =
no bound), item code (-1 = pick one) and a pool mask the item's drop pool
must share a bit with. A set code makes that item whatever its level or
quality (table 31 rows 400 on are quest items, row 0 a mine). Otherwise
the pick is weighted by drop weight among matching items (0x465500,
0x4653a0), with `r % total` of a nine-digit random number (0x401520);
special items are never picked that way. The items land on a circle of
200 units around the monster, spread by the number of rolls. Two slips in
the exe: the allowed qualities are only ever switched on, never off, so a
later roll of the same list may give a magic item its pick does not
allow (our engine clears them each roll, except in original mode); and a
roll whose pick matches nothing makes whatever item the previous roll
made, or a stale one, which we do not reproduce (it drops nothing).

The belt pocket (player +0x518) is a 4 x 2 grid of consumables on the
panel: the top row's cells start at screen (357, 413), the bottom row's
at (405, 445), 32 pixels each (0x445bd0, 0x44a5f0). Picking up a
consumable whose effect word 0 is set puts it in the first free belt
cell, row by row, and anything else (or when the belt is full) in the
bag, column by column (0x449ef0, 0x466f80). Keys 1-4 use the top row and
5-8 the bottom (0x442ed6); a right click on a cell does too, and a left
click picks it up or puts a consumable there. Using a belt item uses up
a like item (same class and code) from the bag when there is one, so the
belt stays stocked, and the belt's own only when there is not. A new
character starts with four Tablets along the top row and four Capsules
along the bottom (0x44104c), besides those in the bag.

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
  stats in column 0 and four classless levels, 900 for the hero and 901
  for the heroine (the table is `0x385 - [character+0x28]` and the sex
  byte is 1 for the hero, see A new game); 902/903 the warrior, 904/905
  the wizard (905, the witch, keeps 15 health a level to level 25 and 50
  magic to level 21), 906/907 the hunter. The odd tables are otherwise
  the same as the even ones. Each level-up first stores the class held
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
| weight (0x28) | weighed against the hero's strength in the item window |
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
have item level and nine effect words (word 0: picked up into the belt; 1:
health restored; 2: mental power restored; 3 and 4: health and mental
power in percent; 5 and 6: the partner's health, as a number and in
percent; 7 and 8: medicines). Special items have nothing more.

Modifiers are rolled once when the item is made (0x467310 weapons,
0x466570 armour, 0x465730 accessories): each one with `chance` percent
becomes a number from min to max, else 0. Modifiers 0-12 add to the
hero's attack, defence, hit, evasion, magic attack, magic defence, magic
hit, magic evasion, attack speed, walking speed, max HP, max MP and
strength (0x44ea60); 30-38 add to the partner's HP, attack, hit,
defence, evasion, magic attack, magic hit, magic defence and magic
evasion while the hero wears the item (0x4138b0, each stat at least 1).
The elemental modifiers add to the item's elements (0x44fe30). Table 34
prices them: its first 8 numbers per point of each elemental modifier,
the next 39 per point of each modifier (modifier 16 on a weapon counts
as |1 - value|). Saves keep the rolled numbers after the item's wear.

Each item the game makes keeps its own durability left (+0xbc, out of
+0xb8 from the record) and an identified flag (+0x1c). Magic and unique
items (quality 1 and 2) are made unidentified and show the second name
until then (0x462d30). Each blow the hero aims wears the weapon one point
30% of the time; each blow that reaches the hero wears the helmet and
boots 20% and the body armour and shield 30% of the time (not a shield
behind a two-handed weapon). An item at 0 is broken and adds nothing to
the hero's stats (0x44eb5b). Mending one costs nothing when it is unworn,
else max(1, worn points x (value / 10) / durability) (0x4667a0), where
the value is the price plus the modifiers' surcharges unless the item's
fixed-price flag (+0x44) is set (0x4661c0). Shops sell at the value and
buy at value x durability left / durability / 4, at least 1.

Axes and two-handed swords (subtypes 1 and 3) or the two-handed flag hide
the shield. Worn weapons, body armour and shields turn on their hero
layer in `Player/Male/Animation00.Caf` (0 body and 1 shadow are always
on): 5-8 armour, 9-11 shields, 12-21 weapons. Helmets, boots and
accessories are not drawn.

## A new game (ShadowFlare.exe 0x440f70)

The title screen's New Game opens the Create New Character screen
(0x421a00 sets it up, 0x423ca0 runs it each frame), drawn from
`System/Select/Pattern/Select.njp`, whose patterns carry their screen
positions:

| Patterns | What |
| --- | --- |
| 41, 36 | the background and the title, CREATE NEW CHARACTER |
| 0/1, 2/3, 4/5 | EXIT, BACK and OK, dim/lit (lit under the pointer) |
| 7/8, 9/10 | the hero and the heroine, dim/lit; 11 and 12 their shades, drawn at half strength |
| 34/35 | NAME, dim while choosing, lit while typing |
| 38; 22/23, 24/25, 20/21 | the game-mode window: ONLINE MODE, SINGLE MODE, BACK |
| 37, 6/13, 14-19, 26-33, 39, 40, 42-65 | the Load Game screen's (LOAD DATA, DELETE, YES/NO, slot numbers) |

Clicking a figure (hero x 168-302, heroine 390-489, y 107-378; the arrow
keys and Enter too) sets the new hero's sex byte (+0x18 of the record that
becomes player +0x28): 1 for the hero, 0 for the heroine. Over 20 frames
the other figure fades out and the chosen one slides 97 pixels to the
middle, and an edit box opens at (190, 407) for the name (the manual: under
20 characters), drawn at (194, 411) in white over a black shadow. OK (or
Enter) with a name sets the job to 16, mercenary, and opens the mode
window; Enter with no name, or BACK, slides the figure back. In the mode
window SINGLE MODE fades out over 14 frames into the game; BACK returns to
the name. GETPLAYERSEX (0x433b1f) reads the sex byte; the player's
animation is `Player\Male` or `Player\Female` by it (0x412806).

0x440f70 then fills in the hero, starting stats from table `0x385 - sex`
(900 for the hero, 901 for the heroine) and:

- saved scenario and entry (+0x15a4, +0x15a8) both 0, so every new game
  begins in scenario 0, Remote Town (`Map\f00_01`), at entry point 0, just
  inside the town gate;
- wearing body armour 0 (+0x4ec), with four of consumable 0 and four of
  consumable 10000000 in the first two columns of the bag (+0x514) and the
  same again in a 4 x 2 list at +0x518 (not used by our engine yet);
- partner kind 0, Kerberos, INACTIVE, with every kind at level 1.

What follows is scripted in scenario 0. On entering, its start sentence
stocks Malse's shop and names the area. Speaking to Ostare (12000000) the
first time runs four boxes and drops the army's supplies at his feet:
weapon 0, armour 1000000, weapon 100 and 200 gold (TEMP 1000020-1000026).
Syria the healer (12000002) gives the first mission, `SETQUESTFLAG 0 1 0`
(the goblins north-east of town). `SETQUESTFLAG quest state` (0x433f29)
sets QUEST[quest]; state 2 clears a mission only when it is 1, taken.

## Missions and the compass

The game object at 0x482df4 keeps the mission states at +0xa0c4 (a
pointer, with the count at +0xa0c0; `DAT_0048ceb8`), saved with the game.
`SETQUESTFLAG quest state flag` stores any state but 2 as it is and plays
sound 0x41; state 2 first checks a second array at 0x48cebc: only if that
mission was never cleared before does it mark it cleared there and, when
the state is 1, set it to 2 and play sound 0x42. The second array is not
saved, and it is marked even when the mission was not taken, so the first
clear can be spent before the mission is given. The third operand only
matters in network play (it tells the other players). Our engine clears a
taken mission every time; original mode keeps the original's way
(`Rules::missionsClearOnlyWhenTaken`).

Titles are Table.Tbd table 41, row = mission; descriptions are table
700 + mission, one line per row.

The mission list (flag 0x48ce54, drawn by 0x40cea0 on layer 14) fills the
screen above the bottom bar with Status.njp pattern 10. The page buttons
are patterns 110 and 111, lit 112 and 113, at (276, 10) and (344, 10);
the page is +0xa910. Page p shows missions 24p to 24p + 23: mission m in
column (m / 12) % 2, row m % 12, with pattern 25 (a closed lock, mission
taken, title in grey 224) or 26 (open, cleared, grey 128) at
(52 + 294 col, 48 + 27 row) and its title 27 right and 6 down, with a
black shadow a pixel down and right. A click on a row's 294 x 27 strip
opens its description (+0xa97c, -1 none): pattern 59 tinted (800, 1000,
1200) under the frame 58, the title as `[%s]` at (190, 96) in (224, 192,
0) and the lines from (190, 120), 16 apart. A click above y = 413 closes
the description. Q (0x4429b0), the Settings Menu row and a click on the
lock or the mission's name toggle the list; opening it closes the other
windows (0x4170f0) and the description.

The bottom line (0x4050f0): `DRAWQUESTNAME quest` (0x433868) sets the
mission +0xa908 and a timer +0xa90c of 600 frames; while it runs the
title shows as `[%s]` in grey 224 with its right end at x = 612 (588
when the class-change icon blinks at 616), y = 368. While any mission is
taken, StatusIcon.njp pattern 0 (a lock) sits at (616, 360). Both hide
while a window covers the right half of the screen.

`SETCOMPASSES msg` (0x43389b), for the player's own party only, copies the
message to +0xa804 and zeroes the counter +0xa800. 0x412220 then draws
`Player/Common/Compasses.Caf` (animation block 501) chart 0, direction 8,
at the top left of the screen, one frame a tick (eight) and then the last;
from tick frames + 150 it fades over 30 ticks, and at frames + 180 the
counter goes back to -1. The name is at (108, 32) in grey 224 with a black
shadow. While the chat line is open (0x48ce5c) the compass stays, at its
last frame.

The open content does the same in its Remote Town (`Scenario/10000000`,
`content.txt` `start=10000000`) with our own words.

## The game panel (ShadowFlare.exe 0x4039f0)

The bar along the bottom of the screen is `System/Game/Pattern/Bar.njp`,
loaded as sprite bank 5 (the table at 0x47f6e8 numbers the game's UI banks:
4 Status, 5 Bar, 6 JobIcon, 7 Dark, 8 Hukidasi, 9 MagicIcon, 10
MagicBarIcon, 11 StatusIcon, 13 MapIcon, 14 WaitIcon, 15 Card). Every
pattern carries its screen position, so it is drawn at (0, 0):

| Pattern | What | Where |
|---|---|---|
| 0, 1, 2 | HP gauge: filled, being gained (bright), being lost (dark) | (81, 426), 206 x 12 |
| 3 (4, 5) | MP gauge (4 and 5 are never drawn) | (106, 453) |
| 6 | one pixel, the glints on the gauges | |
| 7, 8 | the panel: its top edge, and the rest | (0, 400), (0, 412) |
| 9 | MINE lit, with a mine in hand (+0x328) | (496, 424) |
| 10, 11 | WALK dimmed (run is the default), RUN dimmed | (368, 464), (360, 448) |
| 12, 13 | increased power: blinking when it can be raised, pressed | (25, 408) |
| 14, 15 | EXP gauge and its rail | (530, 395), (515, 393) |
| 16, 17, 18 | ITEM, MENU, STATUS lit (alpha 300) for a frame after a click | |
| 19-28 | the level's digits, 6 x 9 | drawn where the code says |
| 29-32 | the companion's gauge (see the partner notes) | |

Each frame (30 a second):

- HP is `hp * 206 / maxHp` pixels (at least 1 while alive). The drawn width
  (+0xa028) follows it 2 pixels a frame; while it is longer, the part being
  lost is pattern 2, while shorter the part being gained is pattern 1. The
  clip rectangles end a pixel short of the width (RECT right is exclusive).
- MP is the same at 4 pixels a frame, pattern 3 only.
- Glints: 2 x fps slots per gauge. An empty slot starts one when
  `(rand() & 0x7f) / 120 == 1`, with speed `(rand() & 7) + 5` and a row
  `rand() & 7`. At age a it is at x = a * a * speed / 200 from (83, 427)
  (MP: (108, 454)), one row lower at random, drawn additively, 2 pixels
  wide and alpha 15 x speed for odd speeds, 1 pixel and 22 x speed for even
  ones; it goes out at the gauge's end less 3.
- The level (+0x194) is drawn with 1 to 3 digits; the ones digit's corner
  is at x 60 (one digit), 65 (two, tens at 56) or 69 (three, 60 and 51),
  y 465. When it changes, +0xa038 counts 10000 to 10008: the ones digit
  is drawn 1.5 times bigger at 10000 and shrinks over 10001-10003 (scale
  166/1000 a frame), the tens at 10004-10007 and the hundreds at 10008; a
  digit grows up and left from its anchor (x + 8, 477).
- EXP is `exp * 109 / need` (table 13, row level - 1), nothing at level
  100, full once exp reaches the need. From 90% it blinks bright (1.5)
  two frames in four.
- RUN or WALK: pattern 11 while walking is the default, 10 while running
  (0x48ce80, saved with the settings).

Clicks on the panel (0x445d10): MENU x 589-639, y 402-413 opens the menu;
RUN x 360-390, y 448-457 and WALK x 368-398, y 464-473 set the default;
STATUS x 537-577, y 420-437 and ITEM x 583-636, y 429-448 open or close
their windows; the power button x 25-58, y 408-430 when it blinks; MINE x
496-511, y 424-439. Any other click at y 412 or below is used up by the
panel. Windows are drawn above it (layer 14 against the panel's 11).

The magic/attack bar above it (0x404ee0, clicks 0x447570) is drawn in
layer 13 from banks 9 (MagicIcon.njp, 26 x 26: 0 the attack, spell + 2)
and 10 (MagicBarIcon.njp, 16 x 16: 2 the attack, 3 an empty slot, spell +
4). The eight F1-F8 slots (+0xa714) run from x 224, 4 pixels more before
each group of four: an empty slot or a spell not learned (player + 0x1440
+ spell x 4 is not 3) is MagicBarIcon 3 at y 392, the chosen spell
(player + 0x127c) MagicIcon spell + 2 at y 382, 26 wide, any other
MagicBarIcon spell + 4 at y 392, 16 wide. The attack follows 4 further
on: MagicIcon 0 at y 382 while chosen (player + 0x1288), else
MagicBarIcon 2. While a window covers the left of the screen the bar
starts at 344, while the item window is open at 124, and with both it is
hidden.

## The item window's totals (ShadowFlare.exe 0x408a80, 0x4074e0)

The item window (Status.njp patterns 2 and 3 at x = 320) has no hero
stats; those are in the status window. Its top left holds, in screen
pixels:

| what | where | colour |
|---|---|---|
| "Total Gold" and the gold | label at (342, 30), number ending at x = 471, y = 28 | label tan |
| the mines' power: table 23 at the hero's level plus the Mine Effect worn, at least 1 | number ending at 471, y = 80 | tan when the Mine Effect worn is above 0, red below |
| mines carried "/" the most | numbers ending at 446 and 471, y = 116, the slash at (445, 117) | tan when the most is above the base 10, red below |
| Status.njp pattern 0x43, a mine, in the round socket | at its own position (336, 84), only while a mine is carried | |
| the weight of everything worn (item 0x28; a shield hidden by a two-handed weapon does not count, broken items do) | number ending at 471, y = 224 | red when above the hero's strength |
| the weight gauge | 33 marks of 3 x 5 pixels, 4 apart from (338, 239) | see below |

POWER, STOCK, WEIGHT and OVER are part of the window's picture. Numbers
(0x405590) are written a digit at a time, 8 pixels apart, ending at the
given x and 2 pixels below the given y, over a black copy one pixel down
and right. The colours are (224, 224, 224), (224, 64, 64) and
(224, 192, 128).

The gauge fills weight x 33 / strength marks (all 33 at strength 0 or
less). The marks are Status.njp pattern 0x12 left of x = 438, 0x13 to
x = 457 and 0x14 (red) after. The marks shown step one a frame towards
that number; the ones between it and the marks shown fade out along the
way, and on the first frame of a change the full marks are pattern 0x15,
white. Holding an item over the book at (336, 140)-(391, 211) fades in
pattern 4 over it.

The status window (Status.njp pattern 5, 0x405750) writes its numbers the
same way, with Font01 like the item window: level (303, 41), experience
(303, 65), HP now and most ending at 81 and 124 with the slash at (83, 91)
(y = 89), strength (95, 113), attack (198, 113), defense (303, 113), hit
rate (150, 137), evasion rate (303, 137), walking speed (150, 161), attack
speed (303, 161), MP like HP at y = 192, magic attack (150, 217), magic
defense (303, 217), magic hit rate (150, 241) and magic evasion rate
(303, 241). Each is tan when what is worn raises it above the hero's own
and red when it lowers it; HP and MP take the tone of their most. The
class goes at (22, 42) and the hero's name at (92, 42). The element
levels are bars, patterns 0x24 (level -10 or less) to 0x38 (+10 or more)
at their own position, each row 16 pixels below the last; pattern 0x39
marks the hero's place on the attribute disc at (80 + x x 48 / 20000,
330 - y x 48 / 20000), fading in and out over 50 frames. Not done yet: a
bowgun's attack shown scaled for anyone but a hunter, and Max at level 100.

Gold is not a counter: it is carried in the bag as Gold items (special
0, 1 x 1, Item0000.njp pattern 270) holding up to 10000 each in the
item's word at 0x40, and Total Gold adds them up (0x466d70). Gold coming
in (0x466e10) first checks there is room for all of it (what the stacks
lack of 10000 plus 10000 a free cell), then tops up the stacks in bag
order and puts new stacks in free cells, row by row. Paying (0x466da0)
takes from the smallest stack first, removing stacks it empties.

While a window covers half the screen the view keeps the hero in the
middle of the other half (0x41e8f0; the open windows are counted at
0x416fb0): the item window on the right puts the hero at x = 160, a window
on the left (status, magic, GATE, map, a shop) at x = 480. With both halves
covered (or the mission list) the world is not drawn at all.

An item under the mouse is described in a box centred under the pointer,
8 pixels below it and kept on the screen, edged in white at half strength
(0x409160).

## The GATE window

`WARPGATE n` (0x4334da) opens the warp gate window and `DELETEWARPGATE n`
(0x433544) closes it. Every town gate's script does the same thing: each
tick `CALCMYPLAYERDIST` measures from the hero to the ground round the gate
(object 10000202); at 0 the gate is found (`SETFLAG WARP[n] 1`, n the
gate's row of table 40), its name floats over it (`MESSAGEBOX`) and a
sound plays once; further away `DELETEWARPGATE 0` closes the window; and a
click on the gate (object 10000200, talk event) runs `WARPGATE 0`. The
operand is the gate's slot in that scenario, not its number: a script
with two gates (01000004, 02200003) uses 0 and 1.

`WARPGATE` does nothing while the window is open or while 0x48ce44 or
0x48ce6c is set (two whole-screen windows; the engine counts the shop as
one); otherwise it closes every other window
(0x4170f0), sets the window's flag (0x48ce50, the UI's +0xa05c) and keeps
`-1 - n` in 0x48d72c. `DELETEWARPGATE n` closes it only if that is the
same n. The other windows' keys (S, I, M, Q, N), Esc and a right click
left of x = 320 and above y = 412 (0x44acd0) close it too; any other click
there is used up by the window (0x448490). It is a left-half window
(0x416fb0), so the magic bar moves right while it is open.

The WARP flags are an array of ints on the player (0x48ceb0, count
0x48ceac), saved with the game; a new game sets WARP[0] (Remote Town's
gate, 0x4412dc). Story scripts set some too: Fanann (02200000) sets
WARP[25], the South Camp of Yugunos, which is how episode 3 opens.

The window (0x40c950, drawn on layer 14) lists Table.Tbd table 40's rows
whose WARP flag is set, in table order. A row is the gate's name (column
0), then the scenario (column 1) and entry point (column 2) it leads to:

| Row | Gate | Scenario | Entry |
|---|---|---|---|
| 0 | Remote Town | 0 | 50 |
| 4 | Cold Svalt Town | 1000000 | 50 |
| 13 | Kanfore, Mining Town | 2100000 | 50 |
| 25 | South Camp of Yugunos | 3900000 | 50 |
| 40 | Berini Village | 4900001 | 50 |
| 50 | Tower of Ordeal, Top Floor | 99000023 | 50 |

(51 rows in all; the first area's gates outside the towns use entries 2
to 6.)

Status.njp pattern 13 is the window (320 x 416, GATE along the top). Ten
gates a page (+0xa934, zeroed with the rest of the window state at 0x41d74b; the
engine does it on each scenario):
row r's button is pattern 22 at (52, 59 + 30 r), its name at (79, 65 + 30
r) with a black shadow a pixel down and right, grey 128. With the mouse
over x 32-288, y 63 + 30 r to 85 + 30 r the name is grey 224 and the
button pattern 24 (lit), or 23 (pushed) while the left button is held, when
the button and the name move a pixel down and right. A click there plays
sound 58, sets the player's scenario change (+0x440 scenario, +0x444 entry,
+0x43c = 1, as `CHANGESCENARIO`) and closes every window. Pattern 11 (the
previous page, at (45, 368)) shows on every page but the first and takes
clicks at x 28-92, y 368-386; pattern 12 (the next, at (243, 368)) shows
while there is a later page, x 224-288. The page number is `"%2d / %2d"`
(the slash is the full-width one), page and pages, centred on x = 85 + 153
/ 2 at y 370 in grey 224 with a shadow; it is not drawn when no gate is
found.

The engine follows this in src/game/warp_gate.cpp. The open content pack
has `gates.txt` in place of table 40 and its own window picture (see
content.md).

## Darkness and the area map

**Darkness** (frame code 0x41e4f0). With Display Darkness on (option
0x48d538), the view not split and the area's light (+0xa9f8, from the
Mct's last word) not 2000, the frame copies `System/Game/Pattern/Dark.njp`
part 0 (8-bit, 640 x 420, an ellipse brightest at the middle) into a light
buffer, adds part 3 (160 x 110) at each player and part light + 1 at each
character whose Mct light word is not -1, skipping dead monsters. The add
is RKC_DIB::TransferToDIBEx mode 4, a saturating add (its table at
HISPEEDMODE + 0xfa300 is min(255, a + b)). The buffer gets a grey palette
from the light L: value i becomes i + i (L - 1000) / 1000 up to 1000 and
i + (255 - i) (L - 1000) / 1000 above, and is drawn on layer 0x12 in mode
0x1009, which multiplies the top 420 rows of the screen. Dark.njp's parts
1-6 are 90 x 60, 120 x 80, 160 x 110, 200 x 130, 240 x 160 and 280 x 200.

**The area map** (0x40d4d0, open while +0xa070 is set). N (0x4429b0) or
the Settings Menu's MAP row (0x4103c0) toggle it; opening it closes the
other left-hand windows, the mission list among them. It clips to (32, 40)
- (319, 375), fills it black and draws the scenario's own
`Scenario.Njp` pattern 0, the whole area at a tenth of its size, with the
hero at (160, 210) less the scroll; the arrow keys scroll it 16 across and
10 down, Enter centers it again. Over it goes the explored mask in mode
0x1024, so unseen cells stay black. The mask (0x417f30) is one byte per
cell of the map picture, saved as `Save\M<scenario><slot>.msk`; while
the hero's screen position changes, 0x418370 marks the 68 x 46 cells
from ((x - 340) / 10, (y - 230) / 10), about a screen. The hero is
`MapIcon.njp` pattern 0 (an arrow) and 1 (PLAYER), shown 15 frames of
every 20. The frame is `Status.njp` pattern 0x47 (320 x 415) and its gem
pattern 0x76, pulsing over 60 frames; the area's name (the compass's,
+0xa804) sits at (72, 50) on a half-dark box. Clicks left of x = 320 and
above y = 412 go to the map, and a right click there closes it.

## Not decoded yet

| Signature | Files | Notes |
| --- | --- | --- |
| `MAPED_FILE v000` | `.map` | 36 bytes; small header read by `RKC_RPGSCRN::ReadMapFile` |
| `VoiceData  V003` | `.Voc` | Music and voice (`RKC_DSOUND.dll`) |
| `ShadowFlare0005` | `Save/*.Ssv` | Save games |
