# Open content pack

The engine can run on open replacement content instead of the original
game's files. A content pack is a folder with a `content.txt` at its root;
`content/` in this repository is one: our own ShadowFlare-style art,
made from scratch in Blender by the scripts in `tools/art/` (see
"Rebuilding content/" below).

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
| `System/Game/Parameter/Item.Ibn`      | `items.txt`                              |
| `System/Game/Pattern/Item%04d.njp`    | `System/Game/Pattern/Item%04d.sheet`     |
| `Character/ITEM/%08d/Pattern.njp`     | `Character/ITEM/%08d/Pattern.sheet`      |

With no `--data`, the engine looks for the original game in `game/` and
falls back to `content/`.

## Text files

All are plain UTF-8 text, one entry per line. `#` starts a comment.
Settings are `key=value`; lists of numbers are separated by commas.
Sections start with a `[name]` line.

### content.txt

    name=ShadowFlare open content
    start=meadow             # map to open first
    projection=15,10         # screen pixels per 100 world units, x and y

The original game uses a 15/10 projection (a 3:2 diamond), and so does
our art. A pack drawn for another angle can set its own; the Flare
importer (below) uses 20/10, for Flare's 2:1 art.

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

A character can have several layers per frame, like the original's
`.Caf`: the body, then one layer per piece of equipment, numbered as in
the original (see `layer=` in items.txt). A line is then

    <direction>[.<layer>][@<priority>]=<frame>[@<priority>],...

    [chart 0 loop]
    0@20=0,1,2,3             # direction 0, layer 0 (the body), priority 20
    0.5@10=4,5,6,7           # direction 0, layer 5 (cloth armour)
    0.9=8@3,9@3,-,10@15      # layer 9 (a shield): per-frame priorities

A frame of `-` draws nothing. Layers are drawn from the highest priority
down, so a lower number is nearer the viewer (the original's rule); equal
priorities are drawn in layer order. Priorities default to 0, so a file
with only `<direction>=` lines is a one-layer character as before. Every
layer of a direction should have the same number of frames. Monsters and
townspeople show all their layers; the player shows layer 0 and the
layers of what they wear (body armour, weapon, and shield unless the
weapon is two-handed).

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
    enemy=1,9840,12720,4,30,4,480,goblin,1,30,2,12
    person=2,1000,1000,0     # kind, x, y, direction
    item=0,0,3120,1680       # class, code, x, y
    exit=16800,12000,17760,12960,2,1

`enemy` is kind (folder `Character/ENEMY/<kind as 8 digits>`), x, y,
direction, hit points, attack, wander radius in world units, and the AI
list it follows, optionally followed by its level (which items it may
drop), the chance in percent that it drops gold and the least and most
gold, then the experience it gives, and then its defence, hit and evasion
(0, 50 and 0 when left out, the original's usual values). Without an
experience field, a monster gives a third of its hit points plus its attack.
Combat follows the original: a blow lands with a chance of hit minus
evasion (20 to 98 percent) and does 90-110% of the attack minus the
defence, at least 1. `tools/content/balance.py` sets these numbers for our
maps from the level the player should be at on each; the `# base` comment
on each line is the monster's strength it starts from. `item` places an item from `items.txt` on the ground. `exit` is a world rectangle and the scenario and entry it
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

### items.txt

    [item weapon 0]          # class and code, as in the original's Item.Ibn
    name=Short Sword
    subtype=0                # see below
    size=2,2                 # inventory cells (32 pixels) across and down
    price=400
    icon=0,0                 # System/Game/Pattern/Item0000.sheet, frame 0
    ground=7,0               # Character/ITEM/00000007/Pattern.sheet, frame 0
    level=1,1                # item level (for drops), required level
    stats=20,100             # see below; numbers left out are 0
    layer=12                 # hero animation layer shown while worn
    weight=10                # how often it drops compared with others

Classes are `weapon`, `armour`, `accessory`, `consumable` and `special`
(gold is special 0). Weapon subtypes: 0 sword, 1 axe, 3 two-handed sword,
5 bowgun, 8 staff, 9 rod (1 and 3, or `two-handed=1`, leave no hand for a
shield). Armour subtypes say where it is worn: 0 helmet, 1 body, 2 shield,
3 boots. `stats` are attack, hit, defense, evasion, magic attack, magic
hit, magic defense, magic evasion, attack speed and walking speed.
Consumables have `effect=`, nine numbers as in the original: the first is
1 if it may go in the belt, the second the health it restores, the third
the mental power. Other keys: `quality` (0 normal, 1 magic, 2 unique, 3
quest; only normal items drop) and `glow` (a second hero layer).

Codes follow the original where an item stands in for one, so the
starting kit (body armour 0, consumables 0 and 10000000) works with
either. Every key but the section line may be left out. When the folder
of a `ground` picture holds `Animation.anim`, the number is a chart of it
instead, as with the original's animated item folders.

## Drawing the original game with this art

A player who has the original game sees its own maps, people and monsters,
but drawn with the pack's art: the engine reads the maps, scripts and
characters from their copy and draws them with content/'s ground
textures, props, characters and hero. `skin.txt` at the pack's root says
how; whatever it does not cover (and any character the pack has no
`.anim` for) is drawn from the original files as before. This is the
default when both are found; `--original-art` turns it off and
`--skin <pack>` uses another pack.

    props=Map/props.sheet        # the sheet whose named frames [objects] uses

    [material grass]             # a ground texture
    tiles=Map/ground.sheet,240   # sheet, first frame[, period (default 4)]
    tile=320                     # world units per tile edge (the default)

    [ground 00f_ground.njp]      # one of the original's ground banks
    grass=44,50,14               # a material it is painted with, and its
    dirt=47,35,19                #   average colour there

    [objects 00f_obj00.njp]      # one of the original's object banks
    0=copper_tank                # pattern = prop
    21-22=wall_x@-190,-117 wall_x@-43,-117
    16-17=log!                   # ! mirrors a prop left to right
    5=-                          # draws nothing

A material is `period` x `period` tiles of a ground sheet, from frame
`first` on, laid out on their diamond as the map draws them; they must
repeat seamlessly every `period` tiles, as the sheets `tools/art/ground.py`
makes do. The original's ground is painted on screen-aligned chips; each
pixel (smoothed over a few pixels) is given the material whose colour is
nearest, so the original's paths, shores and clearings keep their shape,
and that material's texture is drawn there, tinted to the original's
average colour. `none` paints black.

Props are frames of the props sheet that carry a name (a seventh field on
the `frame=` line; `build_pack.py` writes them). A prop is anchored at the
foot of the original piece's picture (the middle of its bottom edge), moved
by `@x,y` world units if given; a piece may be drawn as several props, so a
long wall becomes two shorter ones. Our props carry their own shadows, so
the original's shadow for a replaced piece is not drawn.

`tools/content/skin_preview.py` checks the rules against the original:
for each piece of a bank it draws the original, our props and the two laid
over each other, with the foot marked.

    python3 tools/content/skin_preview.py game 00f_obj00.njp /tmp/fences.png 13 14

`tools/content/skin_fit.py` suggests the lines in the first place: given
a bank, some of its pieces and the props that might stand in for them, it
slides each prop's silhouette over each piece's (keeping their feet close)
and prints the prop and `@x,y` that overlap best.

    python3 tools/content/skin_fit.py game nomal.njp 23-40 conifer_green conifer_gold

Nearly every piece of the original's maps is covered. Still drawn from the
original: the village's tall stone walls and gate towers (00f_city02,
mon01; none of our props is tall enough yet), blood, bodies and the pits
and holes in the ground (blood00, deadbody00, 00d_blood, jimen_x00,
map03_a), the beds in the fort (ex_obj) and the bones on stakes (hone).

## Rebuilding content/

The art is rendered with Blender's Python module (`bpy`) and laid out
with Pillow and numpy:

    python tools/art/creatures.py hero /tmp/art/hero        # and every other
    python tools/art/props.py /tmp/art/props                # script in tools/art
    python3 tools/art/build_pack.py /tmp/art content
    python3 tools/art/shrink.py content

`build_pack.py` lays out the maps and writes the text files;
`shrink.py` turns the renders into 256-colour pictures, as the original's
are, which keeps content/ about a tenth of the size. The item pictures
and the interface (inventory panel, shop window, speech balloon and
font) are kept in content/ and rebuilt on their own:

    python tools/art/items.py render /tmp/items     # a Python with bpy
    python3 tools/art/items.py pack /tmp/items content
    python3 tools/art/ui.py content

Their order is the ITEMS list in items.py; `content/items.txt` is
written by hand.

## Importing Flare

`tools/content/import_flare.py` builds a pack from
[Flare: Empyrean Campaign](https://github.com/flareteam/flare-game)'s
CC-BY-SA 3.0 art, which was the demo content before our own art:

    git clone https://github.com/flareteam/flare-game /tmp/flare-game
    python3 tools/content/import_flare.py /tmp/flare-game /tmp/flare-pack

Which maps, monsters and player layers to import are set at the top of
the script. Flare's maps are made with [Tiled](https://www.mapeditor.org/),
so new maps can be drawn there, saved in Flare's format and imported the
same way.
