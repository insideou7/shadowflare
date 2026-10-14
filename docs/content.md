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
| `System/Title/Pattern/Title.njp`      | `System/Title/Pattern/Title.sheet` (pattern 0) |
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
| `Table.Tbd` table 40                  | `gates.txt`                              |
| `System/Game/Pattern/Status.njp` (GATE window parts) | `System/Game/Pattern/Warp.sheet` |
| `System/Common/Pattern/Visual01-06.njp`, `Waiting.njp`, `WaitIcon.njp` (story pages) | the same names, `.sheet` + `.png` |
| `Table.Tbd` tables 60 and 800-805     | `partners.txt`                           |
| names, heights and colours in `Scenario.Mct` enemy placements | `enemies.txt` (by monster kind) |
| `System/Common/Pattern/System.njp` (the pointer) | `System/Common/Pattern/System.sheet` |
| `Character/PARTNER/%08d/Animation.*`  | `Character/PARTNER/%08d/Animation.anim` + `.sheet` |
| `System/Game/Voice/Voice00.Voc`       | `System/Game/Voice/<voice>.ogg`, one file a voice |
| `System/Game/Music/BGMnn.Voc`, `System/Title/Music/BGM00.Voc` | the same paths ending in `.ogg` |

With no `--data`, the engine looks for the original game in `game/` and
falls back to `content/`.

## Sounds and music

The pack's sounds are Ogg Vorbis files, mono or stereo, at any rate up to
22,050 Hz (the mixer's). Each effect is its own file, named by its voice
number in the original's `Voice00.Voc`: `System/Game/Voice/006.ogg` is
voice 6, the sound of a blow landing. A missing number is a silent voice,
as an empty one is in the original, and up to five copies of each may
play at once. Each piece of music is the original's `.Voc` path with
`.ogg` in place of `.Voc`, and loops whole. `tools/sound/build.py` makes
them all; [sound.md](sound.md) says when each plays.

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

A `!` right after a frame number marks the frame a blow lands on (the
original's 0x40 flag): the player's melee hit, or a monster's strike, is
dealt when the body layer reaches it. Without one the player strikes on
frame 4 (frame 3 for a shot) and a monster halfway through its attack.

    [chart 5 once]
    0@20=0,1,2,3,4,5,6,7!,8,9   # the hero's sword thrust lands on frame 7

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

`Map/Overview/<name>.png` is the map's picture for the area map (N), a
tenth of its size, drawn ahead of time with `--render-maps` (see
"Rebuilding content/"). Draw them again after changing a map; a map
without one is drawn when the area map first opens on it.

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
defence, at least 1, changed by elements as described in docs/magic.md.

After the 18th field come any of these named fields, for the rest of the
original's monster numbers (slot lists are a/b/c, one per attack or spell):

| Field | What it sets | Without it |
| --- | --- | --- |
| `attacks=` | power of its three attacks | the attack field, three times |
| `hits=` | hit of its three attacks | the hit field, three times |
| `spells=` | its three spells (-1 none) | -1/-1/-1 |
| `spell_power=`, `spell_level=`, `spell_hit=` | each spell's power, Table.Tbd level and added hit | 0, 1, 0 |
| `magic_evasion=` | what spells must beat | 0 |
| `speed=` | per mille of its AI's speeds | 1000 |
| `stun=` | what it takes off a stun's chance and length, and 1 if its stuns may run past 15 ticks | 0/0/0 |
| `word7=` | 100 halves what Counter Burst reflects onto it | 0 |

Every monster in the pack has the original's numbers.
`tools/content/enemy_stats.py` copies them from the player's own copy:
each monster on a map laid out after an original area gets the numbers of
the original placement it stands for. It keeps our AI lists and drop
lists. `item` places an item from `items.txt` on the ground. `exit` is a world rectangle and the scenario and entry it
leads to; entry 0 is where the game starts on that map. The scenario
number is the folder name.

`person` takes an optional fifth field, a name that `script.txt` calls
them by.

Each area names its town, where a Transport gate cast there leads (to
the town's entry 100), and says whether Transport may be cast there:

    town=10000000            # Remote Town
    transport=1              # fields and dungeons; left out in towns

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
commands (docs/formats.md lists them). Optional; Remote Town's
(`Scenario/10000000`) is a worked example.

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
a number, `temp:<id>`, `save:<id>`, `quest:<id>`, `net:<id>` or
`warp:<gate>` (a warp gate found, see gates.txt) for a flag, `shown:<name>`
for whether someone is shown (the flag `SETACTIVE` and `SETINACTIVE`
set), `msg:<id>` for a message, `x:<name>` and `y:<name>` for where
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

### gates.txt

    [gate 4]                 # the number the warp:<gate> flag goes with
    name=Cold Svalt Town     # what the GATE window lists
    scenario=11000000        # where picking it leads
    entry=50

The warp gates, standing in for the original's Table.Tbd table 40 (see
formats.md, The GATE window). The window lists every gate whose `warp:`
flag is set, in number order. A gate is a pair of objects in its town's
`script.txt`, as in Remote Town's:

    object=10001,2726,10298,2977,gate
    object=8800,1337,11100,3837,gate-ground

    [sentence gate-found]
    on=judge gate-ground
    SETFLAG warp:0 1
    MESSAGEBOX gate 0 -160 msg:30 224 224 224 3000

    [sentence gate]
    on=talk gate
    WARPGATE 0

Walking onto the ground round it finds the gate and floats its name over
it; a click on it opens the window. The window's picture is
`System/Game/Pattern/Warp.sheet`, drawn by `tools/art/warp.py`. The
pack's two towns have gates (gate 0 Remote Town, 4 Cold Svalt Town), at
the original's places, with the props sheet's warp circle under them.

### enemies.txt

The name shown over a monster with the mouse on it, by its kind (the
`Character/ENEMY/<kind>` folder its `enemy=` lines name), standing in for
the name, height and colour each of the original's placements carries:

    1=Goblin,55               # kind=name,height
    12=Dragon,147,224,160,96  # with a colour: red, green, blue

The height is how many pixels above its feet the name floats (the label's
own 24 come on top); the colour is light grey (224, 224, 224) unless
given. The pack's numbers sit the name a little over the top of each
picture.

### partners.txt

The hero's companions, standing in for Table.Tbd tables 60 and 800-805
(docs/partners.md says what each stat does). There are six kinds, 0 to
5, with the original's numbers; a new hero has kind 0.

    [partner 0]
    name=Ash
    folder=0                 # Character/PARTNER/00000000
    tint=1000,1000,1000      # red, green, blue in thousandths
    hp=320,70                # 320 at level 1, then 70 more each level
    stun_chance=25,30,35     # the value at each level; 35 from level 3 on

Every kind needs all of `attack_speed`, `walk`, `run`, `hp`, `attack`,
`hit`, `defense`, `evasion`, `magic_attack`, `magic_defense`, `magic_hit`,
`magic_evasion` (the value at level 1, then what each level adds),
`element`, `stun_chance`, `stun_length`, `stun_uncapped`, `jump_area`
and `experience` (the value at each level). A line has 1 to 35 numbers;
the last carries on to level 35.

A companion's pictures are `Character/PARTNER/<folder>/Animation.anim`,
with charts 0 idle, 1 walk, 2 run, 3 hit, 4 death, 5 bite, 6 jump up
and 7 jump down; the death and jump charts have only direction 8. Layer 2
of the run chart, if there is one, is the dash's speed streak (an additive
`*` line), shown only while a dash is flat out. The
pack's four dogs share folder 0, a grey dog recoloured by each one's
tint, and the small dog has folder 1, both drawn by `tools/art/creatures.py`
(`dog`, `smalldog`). Townspeople of kind 1000000 + folder stand in for
them, as in Remote Town's kennel:

    [sentence ash-tick]      # hidden while Ash is out with the hero
    on=tick
    GETPARTNERKIND temp:42
    IF shown:ash = 1 @ash-hide
    IF shown:ash = 0 @ash-show

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
`magic`, `magic2` and `magic3` cast the monster's spells in slots a, b
and c (`spells=`). Monsters that know spells follow one of the `mage`
lists; `python3 tools/content/caster_ai.py content` picks it from their
spells (run it again after `enemy_stats.py`).

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
    load=30                  # how heavy it is to wear

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
`glow` (a second hero layer) and, for bowguns, `shot=kind,spread,speed[,turn[,pierce]]`
(kind 0 stone, 1 arrow, 2 fire arrow, 3 ice arrow; see docs/archery.md). The item window weighs the `load` of
everything worn against the hero's strength, as the original does.
An item with a `load` of 60 or more also sounds heavy when it is taken or
put down, and a weapon that heavy swings with the heavy voice.

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

A player who has the original game sees it with its own art by default.
F12 (or the Art row of the Settings Menu, kept in `options.txt`) swaps to
the pack's art and back while playing, without reloading the map: everyone
keeps their place and what they are doing. `--our-art` starts with the
pack's art, `--original-art` with the original's. With it on they see the
game's own maps, people and monsters drawn with the pack's art: the engine reads the maps, scripts and characters
from their copy and draws them with content/'s ground textures, props,
characters and hero. `skin.txt` at the pack's root says how; whatever it
does not cover (and any character the pack has no `.anim` for) is drawn
from the original files as before. `--skin <pack>` does the same with
another pack.

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
original: blood, bodies and the holes in the ground (blood00,
deadbody00, 00d_blood, the bones and blood of jimen_x00, map03_a), the beds in the fort (ex_obj) and the bones on stakes (hone).

## The first area

The pack's maps are the original's first area, laid out with our art:
the opening fields, Remote Town, the Dusty Ruins, Cold Svalt Town and the forests and four dungeons round it, 29 maps in
all (`Map/f00_*.txt`, `f01_*`, `d01_*` to `d05_*`). Their scenarios are
numbered 10000000 plus the original's number (Remote Town is
`Scenario/10000000`), and their exits lead to each other as the
original's do. A new game starts in Remote Town, as the original's does.

Each can be walked on exactly where the original's can: its judge grid
is the original's, cell for cell (cells of 160 world units, about the
width of a character), for the ground reachable from the map's entries.
Laid over the original, a map looks a little different but plays the
same. Also kept: where the exits and entries are, which monsters and
townspeople stand where, which ground is grass or dirt, and where the
buildings and other landmarks stand (the town walls and gates, palisades
and spike pits among them, each drawn with the prop skin.txt gives its
original piece). What is drawn round the walkable ground is our own:
trees, thinning out away from the paths, outdoors, a rock or a bush where
the original shuts off a little ground in the open, and walls along the
dungeon floors (one down the middle of rock a cell thick). The monsters
have the original's numbers (`enemy_stats.py`, above). The dungeons are as dark as the original's, with
lamps where its torches stand and glows on the monsters that carry one.
The maps exactly as the original draws them are what a player with the
original game sees (see above).

`tools/content/first_area.py` writes them from the player's own copy:

    python3 tools/content/first_area.py <game folder> content

and `tools/content/map_overlay.py` checks them against it, printing for
each map how much of the original's walkable ground is walkable in ours
and the other way round, and drawing them laid over each other:

    python3 tools/content/map_overlay.py <game folder> content overlay.png

## Rebuilding content/

The art is rendered with Blender's Python module (`bpy`) and laid out
with Pillow and numpy:

    python tools/art/creatures.py hero /tmp/art/hero        # and every other
    python tools/art/props.py /tmp/art/props                # script in tools/art
    python3 tools/art/build_pack.py /tmp/art content
    build/shadowflare --data content --render-maps content/Map/Overview
    python3 tools/art/shrink.py content
    python3 tools/art/trim_sheets.py content

The hero and the goblin are MakeHuman bodies (MPFB 2, whose base mesh,
targets, rig and weights are CC0) rather than simple shapes, with armour
cut from the body so it fits. Rendering them needs a checkout of MPFB,
which is run but not copied:

    git clone --depth 1 https://github.com/makehumancommunity/mpfb2.git
    export MPFB_SRC=$PWD/mpfb2/src/mpfb

`build_pack.py` writes the ground, floors, props and text files
(`tools/content/first_area.py` and `enemy_stats.py` then lay out the maps);
`shrink.py` turns the renders into 256-colour pictures, as the original's
are, which keeps content/ about a tenth of the size. `trim_sheets.py` then
cuts the see-through space from round each character frame (only after
shrinking does it show: the renders' faint shadow haze fills each frame)
and packs the frames tightly, so the game loads a character's sheet about
five times faster. The item pictures
and the interface (inventory panel, shop window, speech balloon, area map window and
font) are kept in content/ and rebuilt on their own:

    python tools/art/items.py render /tmp/items     # a Python with bpy
    python3 tools/art/items.py pack /tmp/items content
    python3 tools/art/ui.py content          # or just some: ui.py content map
    python3 tools/art/panel.py content    # the game panel along the bottom
    python3 tools/art/magic_icons.py content    # the magic/attack bar's icons
    python3 tools/art/warp.py content    # the GATE window (after ui.py: it uses the font)
    python3 tools/art/story.py content    # the story pages STARTVISUAL shows
    python3 tools/art/effects.py content    # spell, trap and script effects (or some: effects.py content 0 2)

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
