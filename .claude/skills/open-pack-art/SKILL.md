---
name: open-pack-art
description: Make or improve open-pack art (sprites, characters, props, map pieces, items, UI) for content/ with the tools/art Blender pipeline, compared side by side with the original until close.
---

# Open-pack art

Use this for any new or reworked picture in `content/`: characters,
monsters, townspeople, props, map pieces, item pictures, effects or UI.
`content/` is MIT. Everything in it must be made by us from scratch.

## Hard rules

- Never trace, colour-pick, sample or reuse pixels from the original
  game's art. Match its style, scale, angle and palette by eye, and take
  only measurements (sizes, anchors, angles, timings) from its files, as
  `tools/art/match_attack.py` does.
- The original files live in the user's own `game/` folder (gitignored),
  or `/mnt/project-files/game.zip` in project threads. Never commit them,
  or crops of them, to the repository.
- Only bring in outside assets that are CC0 or MIT. Already in use:
  MPFB 2 / MakeHuman base mesh, targets, rig and weights (CC0; MPFB's
  GPL code is run, never copied) and Quaternius models
  (`tools/art/quaternius/`, CC0). Never use Flare, FreedroidRPG, 0 A.D.,
  Wesnoth (SA/GPL) or Mixamo (Adobe terms). The Flare importer's output
  is CC-BY-SA and must never go in `content/`.
- No AI image generators: their output has unclear ownership and can drift
  toward copying the original's look.

## Pipeline

All art is scripted, so it can be changed and re-rendered.

- `tools/art/studio.py`: shared Blender scene. Orthographic camera at
  the original's 15/10 projection (`WALL_ELEVATION` 45 degrees for walls),
  warm top-left sun, shadow catcher. One Blender unit = 100 world units.
- 3D models: `creatures.py` (characters; `--preview` renders one frame
  per animation, `--only idle,walk`, `--blend` saves the model),
  `props.py`, `town.py`, `dungeon.py`, `cave.py`, `fort.py`, `wilds.py`,
  `castle.py`, `deep.py`, `items.py`, `uniques.py` and the others in
  `tools/art/`. Townspeople are one parametric body driven by `PEOPLE` in
  `creatures.py`. The hero, heroine, goblin and skeleton use MPFB bodies
  (`mpfb_body.py`).
- Flat painted pieces (numpy + Pillow, no Blender): use these where a 3D
  render looks off, such as `cave_mouth_2d.py`. Pinky prefers painted map
  pieces in those cases.
- Packing: `build_pack.py` assembles the pack. Then `shrink.py` makes
  256-colour pictures (224 colours + 7 shadow levels, or 255 + see-through
  for floors). Then `trim_sheets.py` packs character sheets. Run
  `trim_sheets.py` again after `add_props.py`. Full order:
  `docs/content.md`, section "Rebuilding content/".

Set-up in a cloud thread (Blender runs headless; no GUI or Blender MCP
is needed):

    python3.13 -m pip install bpy numpy pillow     # bpy 5.2.x
    git clone --depth 1 https://github.com/makehumancommunity/mpfb2.git
    export MPFB_SRC=$PWD/mpfb2/src/mpfb             # MPFB characters only

Run the Blender scripts with `python3.13 tools/art/<script>.py ...`; the
others with `python3`. Save each model's `.blend` to
`/mnt/project-files/art-models/` when it changes.

## The loop (one sprite at a time)

Pinky liked this loop and wants it used for every sprite and texture.

1. Find the original counterpart and render it for reference only, into the
   scratchpad or `/mnt/project-files/art-tests/`, never the repo:
   `tools/re/render.py <bank.njp> <pattern> out.png` for a single piece,
   `tools/re/animframe.py <dir> <stem> out.png <chart> <dir> <frames>`
   for a character frame, `tools/re/drawscene.py` for a whole map.
2. Build or change our model in its `tools/art` script.
3. Render ours at the same scale. Put it side by side with the original,
   at 2x, with the feet or base lined up on one baseline. Check
   silhouette, height (the original's player is about 15% taller than a
   naive render; see `SCALE_MH`), proportions, colours, light direction
   and the anchor point.
4. Save the comparison to `/mnt/project-files/art-tests/<subject>/`. Post
   it in the thread with a short note of what changed. Pinky answers with
   short visual notes ("darker leaves", "more christmas tree shaped").
5. Tweak and repeat until Pinky is happy. Then check it in game: build
   `content/` as above, run the engine on it and take a screenshot.
6. Open the PR with the script changes and the rebuilt `content/` files.
   Keep `content/` small: always run `shrink.py` (and `trim_sheets.py` for
   characters).

## Animation

- Frames are one game tick each (20 a second); see `ANIMATIONS` in
  `creatures.py`. Eight directions per frame.
- Chart numbers follow the original (`build_pack.py` `PLAYER_CHARTS`,
  `ENEMY_CHARTS`, `PARTNER_CHARTS`). Mark the frame a blow lands with
  `!` in the `.anim`.
- Fit timing and poses to the original by measuring it, never by copying
  frames. `match_attack.py` shows how.
- Equipment is drawn in layers (`layerNN/`, NN = the `layer=` in
  `items.txt`), each with its own draw priority per frame.

## Done means

- The comparison image is posted and Pinky has seen it.
- It looks right in game, in both the open pack and (with the original
  files and `--our-art`) the skin over original maps.
- Scripts, `.blend` files and the shrunk `content/` are all updated.
- The build has no new warnings, and the PR says what was tested locally.
  CI is off.
