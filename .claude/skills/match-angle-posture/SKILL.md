---
name: match-angle-posture
description: Match a Blender-rendered character's camera angle, scale, facing per direction and posture (idle, walk, attack, hit) to the original's sprites by measurement, with tools/art/match_view.py.
---

# Match angle and posture

Use this when a character in `content/` (hero, heroine, monster,
townsperson) should stand, face and move like the original's: after
building or reworking a model in `tools/art/creatures.py`, or when Pinky
says a pose, angle or animation looks off. It works with the
`open-pack-art` skill: that skill owns the art rules and the
side-by-side loop; this one measures and fits.

## Rules (from open-pack-art, unchanged)

- Only measurements come from the original: silhouettes' overlap, heights,
  angles, timings, frame counts. Never trace, colour-pick or reuse its
  pixels, and never commit its frames or crops.
- The comparison sheets show the original's pixels. They go in the
  scratchpad or `/mnt/project-files/art-tests/<subject>/`, never the repo.
- The fitter's output is joint angles for our own model, to tidy by hand.

## Set-up (cloud thread)

    python3.13 -m pip install bpy numpy pillow
    apt-get install -y libegl1 libgl1-mesa-dri    # Workbench needs EGL
    git clone --depth 1 https://github.com/makehumancommunity/mpfb2.git
    export MPFB_SRC=$PWD/mpfb2/src/mpfb
    mkdir game && cd game && unzip -q /mnt/project-files/game.zip

`EGL_BAD_MATCH` lines in the output are harmless. Workbench renders a
silhouette in about 0.05 s, so whole animations check in a minute or two.
Pipe output through `grep -vE "Saved|EGL"`. Don't `pkill -f match_view`
from a shell whose own command line holds that text: it kills the shell.

Original folders: `Player/Male`, `Player/Female` (body = layer 0),
`Character/ENEMY/<8 digits>`, `Character/PEOPLE/<8 digits>` (all layers).

## Steps

1. **Calibrate the camera** (once per kind of character):

       python3.13 tools/art/match_view.py hero <orig>/Player/Male out/cal --calibrate

   It fits an ellipse to where the shield's middle, the sword's middle and
   the top of the head land over the eight directions. Its height/width is
   sin(pitch); where each direction sits round it is the turn. Our renders
   get the same measurement at several pitches, so bias from occlusion
   cancels. Prints the pitch that reproduces the original's, the turn
   between directions, the extra yaw our directions need, the short
   sword's drawn length (a rigid scale check) and the idle height ratio.
   Whole-silhouette overlap cannot find the pitch (it is flat from 30 to
   45 degrees), so don't use IoU for that.

2. **Report posture** for every animation:

       python3.13 tools/art/match_view.py hero <orig> out/rep --anims idle,walk,attack,hit --sheet

   Per direction and frame: IoU and the differences in height, left/right
   reach, lean (head over hips), feet spread and centre. Loops get a phase
   search ("best phase shift of our frames"); a frame-count mismatch is
   reported. `--sheet` writes `<kind>_<anim>.png`: original | ours |
   outlines (red original, blue ours), feet aligned, 2x. Read the summary
   lines (`== anim: iou ...`) first, then the worst frames.

3. **Fix in this order**, re-running the report after each:
   1. Facing per direction (`--yaw` to try, then `creatures.facing`).
   2. Frame count and held drawings: the original often holds one
      drawing over several frames (the hit is 4 drawings over 11 frames).
      Match the count in `HERO_ANIMATIONS` / `rig.animations` and hold
      keys with paired keyframes.
   3. Loop phase (e.g. `HERO_WALK_PHASE`).
   4. Key poses: `--fit hit:6` searches our joint angles to best overlap
      that original frame in all eight directions at once and prints
      `hero_key(...)` arguments. About 3 minutes a pose; run several
      in parallel (4 CPUs). Tidy the numbers, check the sheet by eye:
      a fit can win pixels with a limb on the wrong side.
   5. Body moves off the anchor (knock-back, lunges): the `shift` key
      moves the hips in the model's own space (+x forward).
4. Re-render the real sprites (`creatures.py --only <anims>`), rebuild
   `content/` (build_pack, shrink, trim_sheets, then `retime.py <game>
   content` for the original's timing), post the sheet and before/after IoU numbers in the thread,
   and follow open-pack-art's "Done means".

## Numbers so far (2026-10-17)

| Character | Check | Before | After |
|-----------|-------|--------|-------|
| hero, heroine | camera pitch | 41.8 degrees | unchanged: the original fits the map's 41.8 (measured 36.7 and 41.0) |
| hero, heroine | facing | diagonals skewed 11.3 degrees to the 15:10 diamond | even 45 degree steps, like the original (`facing(d, rig)`) |
| hero | walk IoU | 0.41 | 0.46 (phase +3 of 10 frames, `HERO_WALK_PHASE`) |
| heroine | walk IoU | 0.41 | 0.46 |
| hero | hit IoU | 0.22 (4 frames) | 0.62 (11 frames, 4 held fitted poses, knock-back) |
| heroine | hit IoU | 0.17 | 0.52 (her own `HEROINE_HIT`) |
| hero, heroine | attack sword/shield arms | fitted with the old facing | re-fitted with match_attack.py (sword IoU 0.106 -> 0.118) |

Still to do: the attack's body poses (IoU 0.34 hero, 0.25 heroine; fit
them with `--fit attack:N`); idle stance width (ours about 4 px wider each
side); our short sword draws 36 px long against the original's 48; and
every monster and townsperson (goblin walk IoU 0.45 either way, so its
facing is unproven: check with `--calibrate` before switching it).

IoU around 0.45-0.65 is a good match for these small sprites; limbs are
only a few pixels wide, so one pixel off costs a lot.

Keep this table current when a character gets the pass.
