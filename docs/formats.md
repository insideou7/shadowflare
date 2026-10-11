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
                i16 priority   draw order, low first
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

The enemy block (0x13c bytes) holds the monster's stats. Read as i32
words, these meanings were inferred by comparing 18,754 placements
(goblins near Kanfore have 40 HP, Cold Ruins goblins 400), not taken
from the exe, so treat them as likely rather than certain:

| word | meaning |
|---|---|
| 6 | element |
| 8 | hit points |
| 9 | attack |
| 11 | defense |
| 13 | experience given |
| 14 | level |
| 1-4 | area it wanders in: left, top, right, bottom around its position |
| 15-30 | name of its behaviour list in `Control.aid` (Shift-JIS, NUL-terminated) |
| 49 | chance of dropping gold, percent (usually 10; read at 0x45a030) |
| 50, 51 | gold dropped, min and max |
| 77 | movement speed, per mille (usually 1000) |

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

Opcodes identified so far: 0 `IF a op b sentence` (runs the sentence,
then carries on), 1 `SETFLAG flag value`, 17 `CHANGESCENARIO scenario
entry`. The other events and opcodes are not mapped yet.

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
descriptions. The numeric tables (experience curves, shops, drops...) are
not identified yet.

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
