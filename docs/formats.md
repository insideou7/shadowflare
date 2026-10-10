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
u8   compressed, then the walkability grid (not decoded yet)
```

Chip `(cx, cy)` is drawn at screen pixel `(cx * 64, cy * 48)`.

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

## Not decoded yet

| Signature | Files | Notes |
| --- | --- | --- |
| `CHRAnimation002` | `.Caf` | Character animation charts (`RKC_RPGSCRN_CHARANIM::ReadCafFile`) |
| `MAPED_FILE v000` | `.map` | 36 bytes; small header read by `RKC_RPGSCRN::ReadMapFile` |
| `SFItemDataV0000` | `Item.Ibn` | Item table; RCLIB-wrapped with an extra layer |
| `TABLE DATA V000` | `Table.Tbd` | Parameter tables (`RKC_RPG_TABLE.dll`) |
| `RKC_AIDATA v001` | `Control.aid` | Enemy AI (`RKC_RPG_AICONTROL.dll`) |
| `ScenaScriptV000` | `.Scs` | Quest scripts (`RKC_RPG_SCRIPT.dll`) |
| `MCED DATA v0000` | `.Mct` | Map control data, read by ShadowFlare.exe |
| `VoiceData  V003` | `.Voc` | Music and voice (`RKC_DSOUND.dll`) |
| `ShadowFlare0005` | `Save/*.Ssv` | Save games |
