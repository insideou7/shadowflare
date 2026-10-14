"""Fits our player's sword arm to the original's attack, frame by frame.

    python tools/art/match_sword.py <hero|heroine> <original Player/Male or Player/Female folder>

(run with a Python that has `bpy`, numpy and $MPFB_SRC, like creatures.py).
Reads the original's attack (chart 5) from the player's own copy of the
game: in each of the eight directions of every frame it finds the sword
layer's hilt and the way the blade points on screen. As the camera is
orthographic, those are straight projections of one 3D hilt point and one
3D blade direction per frame, which it solves for by least squares. Then
it searches our sword arm (shoulder, elbow, wrist) for the pose that puts
the hand at that hilt and the blade along that direction, with the rest of
the body as HERO_ATTACK / HEROINE_ATTACK pose it, and prints the table to
paste into creatures.py as HERO_SWORD_ARM / HEROINE_SWORD_ARM.

Only measurements are taken from the original; no picture of it is used.
"""

import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Euler, Matrix, Vector

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "re"))
import caf  # noqa: E402
import creatures  # noqa: E402
import render  # noqa: E402
import studio  # noqa: E402
import upd  # noqa: E402

CHART = 5  # the player's attack
BODY, SWORD = 0, 12  # animation layers: the body and the short sword


def pixels(bank, cell):
    """Screen points (x right, y down, from the feet) a cell's picture covers."""
    r = render.render_pattern(bank, cell["pattern"])
    if not r:
        return np.zeros((0, 2))
    pattern = bank["patterns"][cell["pattern"]]
    x0, y0 = pattern["rect"][0], pattern["rect"][1]
    if pattern["rect"][2] <= 0:
        parts = [e for e in pattern["parts"] if 0 <= e["part"] < len(bank["parts"])]
        x0, y0 = min(e["x"] for e in parts), min(e["y"] for e in parts)
    w, h, px = r
    ys, xs = np.nonzero(np.frombuffer(bytes(px), np.uint8).reshape(h, w, 4)[:, :, 3])
    return np.stack([xs + x0, ys + y0], 1).astype(float)


def measure(folder):
    """{frame: {direction: (hilt, blade vector)}} of the original's sword on screen."""
    chart = caf.parse((folder / "Animation00.Caf").read_bytes())["charts"][CHART]
    bank = upd.parse((folder / "Animation00.Njp").read_bytes())
    out = {}
    for f in range(len(chart["dirs"][0]["layers"][BODY])):
        out[f] = {}
        for d in range(8):
            layers = chart["dirs"][d]["layers"]
            if len(layers) <= SWORD or f >= len(layers[SWORD]):
                continue
            sword = pixels(bank, layers[SWORD][f])
            if len(sword) < 4:
                continue
            body = pixels(bank, layers[BODY][f])
            middle = sword.mean(0)
            axis = np.linalg.svd(sword - middle, full_matrices=False)[2][0]
            t = (sword - middle) @ axis
            ends = middle + axis * t.min(), middle + axis * t.max()
            # The hilt is the end in the hand, the one touching the body.
            near = [np.min(np.linalg.norm(body - e, axis=1)) if len(body) else 0 for e in ends]
            hilt, tip = ends if near[0] <= near[1] else ends[::-1]
            out[f][d] = (hilt, tip - hilt)
    return out


def projections(scene):
    """2x3 matrices taking a point of the model (facing +X) to screen pixels
    from the feet, one per direction."""
    origin = Vector(studio.anchor(scene))
    matrices = []
    for d in range(8):
        turn = Matrix.Rotation(creatures.facing(d), 3, "Z")
        cols = [Vector(studio.anchor(scene, turn @ Vector(e))) - origin for e in ((1, 0, 0), (0, 1, 0), (0, 0, 1))]
        matrices.append(np.array([[c.x for c in cols], [c.y for c in cols]]))
    return matrices


def solve_3d(proj, seen):
    """The 3D blade direction and hilt that best give what was seen."""
    vectors = {d: v for d, (h, v) in seen.items()}
    hilts = {d: h for d, (h, v) in seen.items()}
    for _ in range(3):
        blade = np.linalg.lstsq(np.vstack([proj[d] for d in vectors]), np.concatenate(list(vectors.values())),
                                rcond=None)[0]
        # A blade seen end on can have its ends mistaken: flip any that
        # disagree with the fit and take the hilt from the other end.
        for d in vectors:
            if (proj[d] @ blade) @ vectors[d] < 0 and np.linalg.norm(proj[d] @ blade) > 8:
                hilts[d] = hilts[d] + vectors[d]
                vectors[d] = -vectors[d]
    hilt = np.linalg.lstsq(np.vstack([proj[d] for d in hilts]), np.concatenate(list(hilts.values())), rcond=None)[0]
    return Vector(blade).normalized(), Vector(hilt)


def local(rig, name, rotation):
    joint = rig.joints[name]
    return joint.matrix_parent_inverse @ Matrix.Translation(rig.rest[name]) @ Euler(rotation).to_matrix().to_4x4()


def solve_arm(rig, pose, blade, hilt):
    """Shoulder, elbow and wrist that hold the blade along `blade` from `hilt`."""
    rig.pose(pose)
    creatures.plant(rig)
    bpy.context.view_layer.update()
    chest = rig.joints["arm_r"].parent.matrix_world.copy()
    best = None
    for ax in [x / 10 for x in range(-8, 11)]:
        for ay in [y / 16 for y in range(-56, 73)]:
            arm = chest @ local(rig, "arm_r", (ax, ay, 0))
            for ey in [-e / 16 for e in range(27)]:
                elbow = arm @ local(rig, "elbow_r", (0, ey, 0))
                miss = ((elbow @ local(rig, "weapon", (0, 0, 0))).translation - hilt).length
                if best is None or miss < best[0]:
                    best = (miss, ax, ay, ey, elbow)
    _, ax, ay, ey, elbow = best
    wrist = min(((((elbow @ local(rig, "weapon", (0, math.radians(wy), wz))).to_3x3() @ Vector((1, 0, 0)))
                  .normalized() - blade).length, wy, wz)
                for wy in range(-180, 181, 3) for wz in [z / 20 for z in range(-32, 33)])
    return (ax, ay, 0), (0, ey, 0), (wrist[1], wrist[2]), best[0], wrist[0]


def main():
    kind, folder = sys.argv[-2], Path(sys.argv[-1])
    scene = studio.reset(samples=4)
    rig = creatures.BUILDERS[kind]()
    studio.camera(scene, 200, 200, target=(0, 0, creatures.HEIGHT[kind] * 0.42))
    proj = projections(scene)
    table = creatures.HEROINE_ATTACK if kind == "heroine" else creatures.HERO_ATTACK
    seen = measure(folder)
    frames = len(seen)
    rows = []
    for f in range(frames):
        blade, hilt = solve_3d(proj, seen[f])
        arm, elbow, wrist, miss, off = solve_arm(rig, creatures.keyframes(table, f / (frames - 1)), blade, hilt)
        print(f"# frame {f}: blade {tuple(round(x, 2) for x in blade)}, hand missed by {miss:.2f}, blade by {off:.2f}",
              file=sys.stderr)
        rows.append(f"    (({arm[0]}, {arm[1]}, 0), (0, {elbow[1]}, 0), ({wrist[0]}, {wrist[1]})),")
    print(("HEROINE" if kind == "heroine" else "HERO") + "_SWORD_ARM = [")
    print("\n".join(rows))
    print("]")


if __name__ == "__main__":
    main()
