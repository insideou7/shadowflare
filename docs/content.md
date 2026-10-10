# Open content pack

The engine can run on open replacement content instead of the original
game's files. A content pack is a folder with a `content.txt` at its root;
`content/` in this repository is one, built from
[Flare: Empyrean Campaign](https://github.com/flareteam/flare-game)
(CC-BY-SA 3.0) by `tools/content/import_flare.py`.

The pack uses the same folder layout as the original game, so the same
game code runs on either; only the file formats differ. Each reader in
`src/formats/open_content.cpp` fills the same structures as the matching
original format.

| Original                              | Open content pack                        |
|---------------------------------------|------------------------------------------|
| `Map/Ground`, `Object`, `Pattern`     | `Map/<name>.txt` + `.sheet` + `.png`     |
| `Character/.../Animation.caf/.njp`    | `Character/.../Animation.anim` + `.sheet`|
| `Player/Male/Animation00.caf/.njp`    | `Player/Male/Animation00.anim` + `.sheet`|
| `Scenario/<n>/Scenario.Mct` + `.Scs`  | `Scenario/<n>/scenario.txt`              |
| `System/Game/Parameter/Control.aid`   | `ai.txt`                                 |

With no `--data`, the engine looks for the original game in `game/` and
falls back to `content/`.

## Text files

All are plain UTF-8 text, one entry per line. `#` starts a comment.
Settings are `key=value`; lists of numbers are separated by commas.
Sections start with a `[name]` line.

### content.txt

    name=Flare demo
    start=goblin_camp        # map to open first
    projection=20,10         # screen pixels per 100 world units, x and y

The original game uses a 15/10 projection (a 3:2 diamond). Flare's art is
drawn for 2:1, so the pack uses 20/10, which makes a 480-unit tile 192x96
pixels on screen.

### .sheet: sprite sheets

    image=goblin_camp.png
    frame=x,y,width,height,anchorX,anchorY

Each `frame` is a rectangle of the image; its anchor is the point that sits
on the sprite's position (a character's feet, a tile's centre). Frames are
numbered from 0 in the order they appear. Images are PNG with alpha.

### .anim: characters

    sheet=Animation.sheet
    [chart 0 loop]           # loop or once
    0=0,1,2,3                # direction: one sheet frame per tick (20 a second)
    1=4,5,6,7

Directions are 0 = south, then counter-clockwise on screen: south-east,
east, north-east, north, north-west, west, south-west. A direction left out
uses direction 0. Chart numbers follow the original: 0 idle, 1 walk; for
the player 3 hit and 5 punch, for monsters 2 hit, 3 death and 4 attack.

### Map/\<name\>.txt

    size=60,60               # tiles across and down
    tiles=480                # world units per isometric tile edge
    cell=160,160             # walkability cell size in world units
    judge=0,0,180,180        # first walkability cell x, y, then columns, rows
    bank=Map/goblin_camp.sheet
    [ground]
    0:12 0:13 . 0:4          # one row of tiles: bank:frame, '.' for none
    [judge]
    ..xx....                 # one row of cells: 'x' blocked
    [objects]
    2640,1200,0,57           # world x, y, bank, frame

Tile (x, y) covers world units `x*480` to `(x+1)*480` on each axis and its
sprite is drawn anchored at the tile's centre. Objects (walls, trees,
tents) are sorted with the characters so they can stand in front of them.

Without `tiles=`, the ground is a screen-aligned grid of `chip=w,h`
pixel chips, as in the original game.

### Scenario/\<number\>/scenario.txt

    title=Goblin Camp
    map=goblin_camp
    entry=0,2640,1200,0      # entry number, world x, y, direction
    enemy=1,9840,12720,4,30,4,480,goblin
    person=2,1000,1000,0     # kind, x, y, direction
    exit=16800,12000,17760,12960,2,1

`enemy` is kind (folder `Character/ENEMY/<kind as 8 digits>`), x, y,
direction, hit points, attack, wander radius in world units, and the AI
list it follows. `exit` is a world rectangle and the scenario and entry it
leads to; entry 0 is where the game starts on that map. The scenario
number is the folder name.

### ai.txt

    [list goblin]
    walk=40                  # speed when walking home
    on=decide attack priority=2 range=..60
    on=decide approach weight=4 duration=30 speed=45 range=..1800

Each `on=` line is one choice for an event, as in the original's
Control.aid (see formats.md): the event, the action, then any of
`priority`, `weight`, `duration` (ticks), `speed` (world units a tick),
`pause`, `extra`, `hp=<min>..<max>` (percent) and `range=<min>..<max>`
(gap to the player in world units; either end may be left out).
`tools/content/ai.txt` lists the event and action names.

## Rebuilding content/ from Flare

    git clone https://github.com/flareteam/flare-game /tmp/flare-game
    python3 tools/content/import_flare.py /tmp/flare-game content

Needs Pillow. Which maps, monsters and player layers to import, and the
monsters' stats, are set at the top of the script. Flare's maps are made
with [Tiled](https://www.mapeditor.org/), so new maps can be drawn there,
saved in Flare's format and imported the same way.
