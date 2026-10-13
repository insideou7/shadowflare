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
| `Player/Female/Animation00.caf/.njp`  | `Player/Female/Animation00.anim` + `.sheet`|
| `System/Select/Pattern/Select.njp`    | `System/Select/Pattern/Select.sheet`     |
| `Scenario/<n>/Scenario.Mct` + `.Scs`  | `Scenario/<n>/scenario.txt`              |
| `System/Game/Parameter/Control.aid`   | `ai.txt`                                 |
| `System/Game/Parameter/Item.Ibn`      | `items.txt`                              |
| `System/Game/Pattern/Item%04d.njp`    | `System/Game/Pattern/Item%04d.sheet`     |
| `Character/ITEM/%08d/Pattern.njp`     | `Character/ITEM/%08d/Pattern.sheet`      |
| `Table.Tbd` tables 41 and 700 on      | `missions.txt`                           |
| `System/Game/Pattern/Status.njp` (mission list parts) | `System/Game/Pattern/Missions.sheet` |
| `Player/Common/compasses.Caf/.Njp`    | `Player/Common/compasses.anim` + `.sheet`|
| `System/Game/Pattern/Status.njp` and `MapIcon.njp` (area map parts) | `System/Game/Pattern/Map.sheet` |

With no `--data`, the engine looks for the original game in `game/` and
falls back to `content/`.

## Text files

All are plain UTF-8 text, one entry per line. `#` starts a comment.
Settings are `key=value`; lists of numbers are separated by commas.
Sections start with a `[name]` line.

### content.txt

    name=ShadowFlare open content
    start=10000000           # where a new game begins: a scenario number or a map name
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

Spell effects (`Character/OPTION/<number>/Animation.anim`, drawn by
`tools/art/effects.py`) use two more marks. Direction `8` is an effect
with no direction (a glow or a burst); the engine uses it whenever the
effect's own direction has no frames. A `*` at the end of the key draws
the line's frames additively, adding their light to what is behind, as
the original draws its flames and glows. A frame can end in
`%<opacity>`, in thousandths (`1000` is solid), before any `@`:

    [chart 0 once]
    8*=0,1,2%600,3%300       # no direction, additive, fading out

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
list it follows, optionally followed by its level (the list in
`drops.txt` it drops from), the chance in percent that it drops gold and the least and most
gold, then the experience it gives, then its defence, hit and evasion
(0, 50 and 0 when left out, the original's usual values), and then its
magic defence and element (0 fire, 1 water, 2 earth, 3 thunder, 4 holy,
5 dark, 6 gel, 7 metal, -1 none; 0 and -1 when left out). Without an
experience field, a monster gives a third of its hit points plus its attack.
Combat follows the original: a blow lands with a chance of hit minus
evasion (20 to 98 percent) and does 90-110% of the attack minus the
defence, at least 1, changed by elements as described in docs/magic.md. `tools/content/balance.py` sets these numbers for our
maps from the level the player should be at on each; the `# base` comment
on each line is the monster's strength it starts from. `item` places an item from `items.txt` on the ground. `exit` is a world rectangle and the scenario and entry it
leads to; entry 0 is where the game starts on that map. The scenario
number is the folder name.

`person` takes an optional fifth field, a name that `script.txt` calls
them by.

Dark places say how light they are and where their lights are:

    light=1000               # 2000 lit (the default), 1000 dark
    lamp=9845,14332,1        # x, y, size 0 (smallest) to 5
    enemy=1,500,600,4,30,4,480,goblin
    glow=1                   # the line before carries a light of size 1

With Display Darkness on, everything outside the light round the hero,
the lamps and anything that glows goes dark. The light's sizes are the
original's (90 x 60 to 280 x 200 pixels); without its `Dark.njp` the
engine draws its own soft ellipses of those sizes. The area map (N) is
drawn from the map itself, a tenth of its size, inside the window in
`System/Game/Pattern/Map.sheet` (frames: the window, its gem, the hero's
arrow, the PLAYER label).

### Scenario/\<number\>/script.txt

What the people of a scenario say and do, in the original's script
commands (docs/formats.md lists them). Optional; Goblin Meadow's is a
worked example.

    person=8,3072,6048,0,merchant   # more people: kind, x, y, direction, name
    object=0,0,640,320,well         # something to click: left, top, right, bottom, name
    temp=1000001,7                  # a TEMP flag and its value when the scenario starts

    [message 2]
    Fresh from the city, every piece of it.
    ~Buy and sell~  ~Leave~

    [sentence merchant]
    on=talk merchant                # when it runs: talk, talk-end, judge, enemy-dead, tick or start
    C_STOP merchant
    TALK msg:2 temp:2 0 0 -1 -1

    [sentence merchant-end]
    on=talk-end merchant
    IF temp:2 = 0 @merchant-shop

    [sentence merchant-shop]
    INITTRADE 0 0
    TRADE 0

A message is the lines under `[message <id>]`, each ending in a line
break; options go between `~` marks. A sentence is a list of commands, one
per line, and `on=` lines that start it. Operands are separated by spaces:
a number, `temp:<id>`, `save:<id>`, `quest:<id>` or `net:<id>` for a
flag, `msg:<id>` for a message, `x:<name>` and `y:<name>` for where
someone stands, `=`, `!`, `>` or `<` in `IF`, `@<sentence>` for a
sentence to run, and a person's or object's name for their id. As in the
original, `IF` runs its sentence and then carries on, and closing any of
a person's message boxes runs their talk-end sentences, so a script keeps
the question it asked in a flag.

    object=2500,2500,2800,2800,chest1,chest_gold
    object=2500,2500,2800,2800,chest1_open,chest_open,hidden

An `object` can also be drawn: after its rectangle and name, a prop of
`Map/props.sheet` drawn at the rectangle's middle. Without a prop it is
an invisible area to click or walk into; with one it stops the hero.
`hidden` starts it shown, usable and in the way all off, like the
original's open chests until the script swaps them (`SETACTIVE` and
`SETINACTIVE`).

### missions.txt

    [mission 0]              # the number SETQUESTFLAG and DRAWQUESTNAME take
    title=Drive off the beast on the north-east road.
    text=Something has been hunting along the road
    text=                    # an empty line
    text=Find it and drive it off.

The title is what the mission list and the bottom of the screen show; each
`text=` line is a line of the description the list opens, up to about 45
characters so it fits the box. The list has room for 48 missions.

An open pack's area names come from each scenario's `title=`, shown by the
compass as the player arrives; a script can still name an area with
`SETCOMPASSES msg:<id>`, as the original's do.

### shops.txt

    [shop 0]                 # the list INITTRADE names
    sell=weapon,0            # class and code from items.txt
    sell=consumable,0,6      # and how many

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
`elements=` is what wearing it adds to the hero's fire, water, earth,
thunder, holy, dark, gel and metal levels (docs/magic.md).
Consumables have `effect=`, nine numbers as in the original: the first is
1 if picking it up puts it in the belt pocket, the second the health it restores, the third
the mental power. Other keys: `quality` (0 normal, 1 magic, 2 unique, 3
quest; only normal items drop; magic and unique ones start unidentified),
`unidentified` (the name shown until then), `durability` (weapons and
armour: the wear they take before they break; none or 0 never wears out)
and `glow` (a second hero layer).

Codes follow the original where an item stands in for one, so the
starting kit (body armour 0, consumables 0 and 10000000) works with
either. Every key but the section line may be left out. When the folder
of a `ground` picture holds `Animation.anim`, the number is a chart of it
instead, as with the original's animated item folders.

### drops.txt

What monsters leave behind, standing in for the original's drop tables
(Table.Tbd tables 30 and 31, see docs/formats.md):

    [drops 2]                # the list number an enemy= line names
    rolls=1                  # how many tries, 1 to 10
    chance=15                # percent each try drops something (15 drops 16 in 100, as in the original)
    gold=10,4,12             # chance, least and most gold, for monsters that have none of their own
    pick=consumable,0        # one of the picks is chosen at random each time
    pick=any,any,1,3         # any weapon, armour or accessory of item level 1 to 3, by weight
    pick=nothing

A pick is a class (or `any` for weapon, armour or accessory), a code (or
`any`, chosen by `weight` among the items of that class), and optionally
the lowest and highest item level. Only normal items are picked with
`any`. A mine (`special,1`) goes to the hero's mines, not the bag (see
[mines.md](mines.md)). Gold comes before the items, 100 units below the monster; the
items land around it.

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

The original's scenario objects (`Character/OBJECT/<kind>/Pattern.njp`:
chests, doors, levers) are skinned the same way, in a section named
after the kind, but their props are anchored on the object's own
position, as the original's pictures are, not on the picture's foot:

    [objects object/00000000]    # Character/OBJECT/00000000
    0=obj_chest_x

`tools/art/objects.py` draws ours to fit: each prop's anchor is set so
the model's outline sits where the original picture's does.

Nearly every piece of the original's maps is covered. Still drawn from the
original: blood, bodies and the pits
and holes in the ground (blood00, deadbody00, 00d_blood, jimen_x00,
map03_a), the beds in the fort (ex_obj) and the bones on stakes (hone).

## The first area

Besides the demo's seven areas, the pack has the original's first area
laid out with our art: the opening fields, Remote Town, the Dusty Ruins,
Cold Svalt Town and the forests and four dungeons round it, 29 maps in
all (`Map/f00_*.txt`, `f01_*`, `d01_*` to `d05_*`). Their scenarios are
numbered 10000000 plus the original's number (Remote Town is
`Scenario/10000000`), so they keep clear of the demo's 1 to 7, and their
exits lead to each other as the original's do. The game still starts in
the meadow; `--map f00_01` starts in Remote Town.

Each follows the original map's routes, not its exact layout: which
blocks of tiles can be walked through, where the exits and entries are,
which monsters and townspeople stand where, and where the buildings and
other landmarks stand. The walls, paths and forest edges are our own, and
the monsters' stats are the demo's. The dungeons are as dark as the
original's, with lamps where its torches stand and glows on the monsters
that carry one. The maps exactly as the original has
them are what a player with the original game sees (see above).

`tools/content/first_area.py` writes them from the player's own copy:

    python3 tools/content/first_area.py <game folder> content

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
and the interface (inventory panel, shop window, speech balloon, area map window and
font) are kept in content/ and rebuilt on their own:

    python tools/art/items.py render /tmp/items     # a Python with bpy
    python3 tools/art/items.py pack /tmp/items content
    python3 tools/art/ui.py content          # or just some: ui.py content map
    python3 tools/art/panel.py content    # the game panel along the bottom
    python3 tools/art/magic_icons.py content    # the magic/attack bar's icons

Their order is the ITEMS list in items.py; `content/items.txt` is
written by hand.

A few new props can be added to the existing props sheet without
rebuilding everything: render them, then

    python3 tools/art/add_props.py /tmp/art/deep /tmp/art/deep/deep.txt earth_tall_x earth_tall_y

## Importing Flare

`tools/content/import_flare.py` builds a pack from
[Flare: Empyrean Campaign](https://github.com/flareteam/flare-game)'s
CC-BY-SA 3.0 art, which was the demo content before our own art. A pack
built this way stays under CC-BY-SA 3.0 and must not be committed to
content/, which is MIT:

    git clone https://github.com/flareteam/flare-game /tmp/flare-game
    python3 tools/content/import_flare.py /tmp/flare-game /tmp/flare-pack

Which maps, monsters and player layers to import are set at the top of
the script. Flare's maps are made with [Tiled](https://www.mapeditor.org/),
so new maps can be drawn there, saved in Flare's format and imported the
same way.
