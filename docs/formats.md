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
| 0x8000 | exit; usually a row of three cells at a map edge. Destinations are not decoded yet |

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
i32  n; 16-byte entries[n]   unknown (areas?)
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
| 15-21 | looks like a Japanese name |
| 50, 51 | gold dropped, min and max |
| 77 | 800-3200, unknown (speed or sight range?) |

Object kinds have only `Pattern.Njp`/`.Sdw`, no animation file.

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

## Not decoded yet

| Signature | Files | Notes |
| --- | --- | --- |
| `MAPED_FILE v000` | `.map` | 36 bytes; small header read by `RKC_RPGSCRN::ReadMapFile` |
| `SFItemDataV0000` | `Item.Ibn` | Item table; RCLIB-wrapped with an extra layer |
| `RKC_AIDATA v001` | `Control.aid` | Enemy AI (`RKC_RPG_AICONTROL.dll`) |
| `ScenaScriptV000` | `.Scs` | Quest scripts (`RKC_RPG_SCRIPT.dll`) |
| `VoiceData  V003` | `.Voc` | Music and voice (`RKC_DSOUND.dll`) |
| `ShadowFlare0005` | `Save/*.Ssv` | Save games |
