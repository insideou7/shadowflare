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

## Not done yet

The original plays effects in about 50 more places, which the engine does
not play yet:

- **Windows and menus.** Clicks and page turns use voices 0x2c and
  0x37-0x41 (0x404e40-0x425830).
- **Spells.** Their casts and hits (0x42a860-0x42f890).
- **The hero.** Steps, swings, hits and level-ups (0x4456d0-0x44b020).
- **Monsters.** Their blows and deaths (0x457fa0-0x45bec0).
- **Companions.** Their blows and hits (0x45fff0-0x461c40).

The open content pack has no sounds of its own yet. It needs its own banks
before the game can be played with sound without the original files.
