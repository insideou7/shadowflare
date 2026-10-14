# Sound and music

The original keeps its sounds in `.Voc` banks under `System`, and plays them
through its own `RKC_DSOUND.dll` on DirectSound. The engine reads the same
files from the player's copy and plays them the same way. Like everything
else from the original, the files are never copied into this repository.

| File | What it holds |
|---|---|
| `System/Game/Voice/Voice00.Voc` | 160 sound effects, 8-bit and 16-bit mono at 5,000-22,050 Hz |
| `System/Game/Music/BGM00-06.Voc` | the area music, 22,050 Hz 8-bit stereo |
| `System/Title/Music/BGM00.Voc` | the title music, 12,000 Hz 8-bit mono |

The code is in `src/formats/voice_bank.cpp` (the reader),
`src/audio/mixer.cpp` (playing and mixing), `src/platform/audio.cpp` (the
sound device) and `src/game/sound.cpp` (when the game plays what). None of it
differs from the original, so original mode has nothing to switch.

## The `.Voc` format

All numbers are little-endian (`RKC_DSOUND_VOC::Read`).

1. `VoiceData  V00n`, padded to 16 bytes. The shipped files are version 3.
2. The voice count, 32 bits.
3. From version 3 on, how many more copies of each voice may play at once.
   It is 4 in `Voice00.Voc` and 0 in the music.
4. Each voice:
   - 32-bit flags, then a 256-byte name;
   - with flag bit 0 set, a 256-byte name of an earlier voice it is another
     name for (none of the shipped files has one);
   - otherwise, a 256-byte path of the file it was made from, a
     WAVEFORMATEX (18 bytes, always PCM), the data size and the samples.

## Playing

- **Banks.** The original keeps the effects in bank 0 and the music in bank
  500. Loading a file into a bank stops what it played.
- **Copies.** `Play` (RKC_DSOUND_VOC::Play) starts the voice from the top on
  a copy that is not playing. When every copy is busy, the one that has
  played longest starts again.
- **Volume.** The two sliders on the Settings Menu go from 0 to 10. 0 is
  silent (-10000 hundredths of a decibel), and each step above it is 3 dB:
  -3000 + 300 × the slider. Effects take the effect volume when they start,
  and the music follows its slider as it plays.
- **Distance** (0x417260). A sound where something happens is only played
  when it is on the hero's map and within 3000 units of the hero, unless it
  is marked to be heard everywhere. Nothing is panned.
- **The device.** The mixer runs in the game's own thread. Each frame, the
  main loop tops up SDL's queue to a tenth of a second, so no sound thread
  calls back into the game. `--record-sound file.wav` saves the mix of each
  tick instead, which gives the same file on every run.

## Music

- **Areas** (0x4275e0). On each scenario change, the scenario's third word
  picks `System/Game/Music/BGMnn.Voc`: 0 towns, 1 fields, 2 the Caravan and
  two towns, 3 boss rooms, 4 ruins, towers and forts, 5 caves, and 6
  shrines. The file is only read again when the number changes.
- **From the top** (0x4275a0). The music plays, looping, on the first tick
  after any scenario change, so it starts again from the top even when the
  area keeps the same music.
- **Title.** The title screen loads `System/Title/Music/BGM00.Voc` into the
  music bank, and plays it looping if it is not already playing. The new
  character and options screens it leads to keep it on.

## Sounds the engine plays

- **`PLAYSOUND voice everywhere x y`** (0x430f80 case 16). It plays an
  effect at (x, y), or where the player stands when x is -1. A nonzero
  `everywhere` skips the distance check.
- **Trap hits** (0x45e1e0). A shot plays its hit sound at its own spot when
  a blow lands, once a tick. Arrows (kinds 0, 1 and 3) have voice 7, and
  fire jets (kind 2) have none. The wall sound of the shots scripts use is
  -1, so they are silent when they hit a wall.

## Menus, windows and items

All of these are heard wherever the hero is. The keys that open and close
windows or press buttons (I, S, Esc, Q, N, M, B, P) are silent, as in the
original: only the mouse makes these sounds.

- **Items** (0x466110). The item picks its own voice. Taking an item into
  the hand, or putting it over another one, plays 0x30, or 0x2f when the
  item weighs 60 or more. Putting it down in empty cells plays the same.
  Wearing an item plays 0x31. Gold always plays 0x55 and accessories
  always 0x5d. These play in the bag, on the belt, on the equipment slots,
  in the warehouse, and when buying or selling. The weight is the Item.Ibn
  field at 0x28; the open content's `load` key gives it (0 when
  left out). The same function gives the hero's swing voice: 1, or 2 for a
  weapon of 60 or more.
- **Picking up** (0x449ef0) plays 0x30 for anything but gold and mines,
  which are silent. **Using** a potion or other consumable (0x44a240) plays
  0x10, but only when it did something.
- **Panel** (0x445bd0). The MENU, RUN, WALK, power and MINE buttons, and
  opening the status and item windows, play 0x3a. Closing those windows
  with their buttons plays 0x39, and so does the MENU button while the
  Settings Menu is open, which closes it.
- **Spells** (0x447570, 0x447790, 0x404e40). Choosing another spell on the
  bar, or the attack icon, plays 0x3a. In the magic window the STATUS tab
  and the page buttons play 0x3a, taking up a spell to drag 0x39, and
  dropping it on the bar 0x3a. The status window's MAGIC tab plays 0x3a,
  and changing class plays 0x40 at the hero.
- **Missions and gates** (0x40cea0, 0x40c950). The mission list's page
  buttons play 0x3a even when that page is showing. In the GATE window,
  choosing a gate and turning the page play 0x3a.
- **Dialogue** (0x457fa0, 0x458080). Clicking an option, or closing the
  box with Enter or Space, plays 0x39. Esc, the arrows, and a click on a
  box without options are silent.
- **Settings Menu** (0x4103c0). Every choice on the ON/OFF rows, the
  click size and the click order plays 0x37, even one already set; the
  volume sliders are silent. Mission List, MAP, HELP and the two save rows
  play 0x38, and Back plays 0x39. Their YES plays 0x38 again and NO 0x37.
- **Warehouses and weapon sets** (0x447ca0, 0x406aa0, 0x44a8f0). The Giant
  Warehouse's tabs and close button play 0x3a, as does changing weapon sets
  with the tabs (Caps Lock is silent). Gold taken with the split box plays
  0x55. A right click that empties a slot of the magic window's bar plays
  0x39.
- **New character** (0x423ca0, 0x424d80). Moving between the hero and the
  heroine plays 0x3a, but only when the choice changes. Choosing one, BACK,
  EXIT, and closing the name box with a name play 0x37. In the mode window,
  moving up or down plays 0x3a and SINGLE MODE, BACK and Enter play 0x38.
- **Missions from scripts** (0x430f80 case 0x3e). SETQUESTFLAG plays 0x41
  when it sets any state but 2, even an unchanged one, and 0x42 when it
  clears a mission that was taken.

## Spells, mines and power mode

Each spell's controller plays its sounds where they happen (0x42a860-0x42f890):

- **Casts.** Fire Ball, Flame Strike, Mud Javelin and Elemental Strike play
  19 as the shot leaves the hand. Ice Bolt and Dread Deathscythe play 94,
  and Medusa plays 22. Where several shots fly, it plays once, at the last
  one. Hell Fire and the companion's Explosion play 29, then 23. Sonic
  Blade plays 154 at the hero.
- **Bursts.** Plasma plays 21 for each burst that is not blocked, and
  Lightning Storm 21 for each ring, even a blocked one. Earth Spears play
  22 for each spear. Ice Blast plays 22 six times, every 3 ticks. Mud
  Javelin's burst and Elemental Strike's earth stage play 22, and every
  Elemental Strike stage plays 19.
- **Hits.** A shot plays its hit sound where it is when a hit lands, once
  a tick. Most spells' shots play 20, and Sonic Blade 155. Plasma, Earth
  Spears and Lightning Storm have none, and no shot sounds at a wall.
- **Heal** plays 17 when it restores health.
- **Mines** beep 54 every 20 ticks from tick 40, and the blast plays 29,
  then 6 when it hits something (0x42bd40).
- **Power mode** hums 76 at the hero every 15 ticks, 60 times in all
  (0x443490).
- **The Transport gate** plays 79 when it is made, and 51 each time its
  animation starts again (0x4201b0).

## Fighting

- **The hero** (0x435e60, 0x439140). A sword, great sword or axe shouts as
  the blow falls, hit or miss: 96 for the hero and 99 for the heroine. The
  swing itself sounds 6 ticks in (5 for any other weapon): 2 for a weapon
  heavier than 59, otherwise 1, and 1 with bare hands. A blow that lands
  plays 6 at the hero. A bowgun twangs 3 at the same point of its pull,
  and its arrows play 7 when they land (0x437fe0, 0x438a90).
- **Steps** (0x4351f0, 0x435530). Voice 0 plays as the hero sets off, then
  every 12 ticks walking or every 8 running.
- **Shields.** Magic Shield and Counter Burst play 60 when they work.
- **Defeat and levels.** The hero's defeat plays 13, or 14 for the
  heroine. Each new level plays 63, and level 5 plays 64 first (0x412fb0).
- **Monsters** (0x45a2f0, 0x45bec0, 0x45a2a0). A monster's blow that lands
  on the hero or the companion plays 6 at the monster. Its death cry comes
  from a table by its Character/ENEMY folder. Some frames of its animations
  carry cue flags (cell status 0x400, 0x800 and 0x1000), and those frames
  play that kind's sound for the animation. Every kind's death frames play
  86 unless its own table has another sound. A monster that is frozen plays
  119.
- **Companions** (0x45fff0-0x461c40). A bite's cue frame plays 95, and 44
  when the bite lands. A jump plays 45 twice as it takes off, and 46 as it
  lands. A dash plays 95 as it starts, for kinds whose table 60 column 6 is
  set, and 6 on each tick it bites something.
- Getting hit has no sound of its own; the only sound is the blow's.

## Not done yet

These sounds belong to things the engine does not have yet:

- **Windows the engine lacks.** The special items page (0x446320), the
  Change Color window (0x40f9e0), the card game (0x40da90, 0x2c, 0x40 and
  0x41) and the online menus. The shop's unpaid
  hand and right-click buying (0x4480c0, 0x44b020), and picking up into
  the hand while the item window is open (0x449ef0), are also missing.
- **Fights the engine lacks.** The Warrior's combos and axe spin
  (0x4364e0-0x437910), monsters freezing the hero (0x443cb0, 119), the revive item
  (17), life absorption (0x459690, 61) and the companion's idle animation
  (0x461230, 88).

## The open pack's sounds and music

The open content pack has its own sounds and music, made for this project
by `tools/sound` (docs/content.md says how they are stored). Nothing in
them is taken from the original: every sound is synthesized from
oscillators, noise and filters by our own scripts, and the music is our
own, written as scores in `tools/sound/pieces.py`.

- **Effects.** `tools/sound/sfx.py` lists every voice number the game
  plays, with a name, what it is for and how it is made: 154 of the
  original's 160 (70-75 are empty in the original too). Each was made to
  do the same job as the original's voice, at a fitting length: a click
  for a button, a cry for a death. Voices the engine does not play yet
  (marked so in the list) were made from where the original's scenario
  scripts play them, such as doors, chests, levers and tower gates, or
  else as the kind of sound their neighbours suggest.
- **Music.** Eight pieces, each a loop of about a minute (the title's
  half that): the title screen in D minor, a country dance for towns,
  a wandering tune over harp for the fields, a desert road song for the
  Caravan, a driving fight for boss rooms, a slow march for ruins, towers
  and forts, a drone with dripping water for caves, and a hymn for shrines.
- **Making them.** `python3 tools/sound/build.py` writes them all into
  `content/` (it needs numpy and ffmpeg with libvorbis), and gives the
  same files on every run. `--sfx 6 29` or `--music town` makes only
  those, and `--wav <folder>` keeps WAV copies to listen to.
