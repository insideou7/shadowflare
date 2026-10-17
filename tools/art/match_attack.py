"""Fits our player's sword and shield arms to the original's attack, frame by frame.

    python tools/art/match_attack.py <hero|heroine> <original Player/Male or Player/Female folder>

(run with a Python that has `bpy`, numpy and $MPFB_SRC, like creatures.py).
Reads the original's attack (chart 5) from the player's own copy of the
game. In each of the eight directions of every frame it finds the sword
layer's hilt and the way the blade points on screen, and the shield
layer's middle and the ellipse it makes. As the camera is orthographic,
those are straight projections of one 3D hilt point and blade direction,
and one shield centre and facing, per frame, which it solves for by least
squares. Then it searches our arms (shoulder, elbow, wrist) for the poses
that put the hand at that hilt with the blade along that line (or, where
the hand cannot reach, the blade pointing at the original's sword tip),
and the shield at that place facing that way, with the rest of the body as
HERO_ATTACK / HEROINE_ATTACK pose it. It prints the tables to paste into
creatures.py: HERO_SWORD_ARM / HERO_SHIELD_ARM (or the HEROINE_ ones).

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
BODY, SHIELD, SWORD = 0, 9, 12  # animation layers: the body, the round shield and the short sword


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
    """{frame: {direction: (hilt, blade vector)}} of the original's sword on
    screen, and {frame: {direction: (middle, covariance)}} of its shield."""
    chart = caf.parse((folder / "Animation00.Caf").read_bytes())["charts"][CHART]
    bank = upd.parse((folder / "Animation00.Njp").read_bytes())
    out, shields = {}, {}
    for f in range(len(chart["dirs"][0]["layers"][BODY])):
        out[f] = {}
        for d in range(8):
            layers = chart["dirs"][d]["layers"]
            if len(layers) > SHIELD and f < len(layers[SHIELD]):
                shield = pixels(bank, layers[SHIELD][f])
                if len(shield) >= 20:
                    shields.setdefault(f, {})[d] = (shield.mean(0), np.cov((shield - shield.mean(0)).T))
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
    return out, shields


def projections(scene, rig=None):
    """2x3 matrices taking a point of the model (facing +X) to screen pixels
    from the feet, one per direction."""
    origin = Vector(studio.anchor(scene))
    matrices = []
    for d in range(8):
        turn = Matrix.Rotation(creatures.facing(d, rig), 3, "Z")
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
    return Vector(blade).normalized(), Vector(hilt), float(np.linalg.norm(blade))


def solve_shield(proj, seen):
    """The shield's 3D centre and facing. A disc of radius r facing n covers,
    seen straight on, points spread as r^2/4 (I - n n^T); orthographic
    pictures of it spread as A (that) A^T, linear in its six numbers."""
    rows, rhs = [], []
    for d, (middle, cov) in seen.items():
        a = proj[d]
        for i, j in ((0, 0), (0, 1), (1, 1)):
            # cov[i, j] = sum over k, l of a[i, k] a[j, l] S[k, l], S symmetric
            rows.append([a[i, 0] * a[j, 0], a[i, 0] * a[j, 1] + a[i, 1] * a[j, 0], a[i, 0] * a[j, 2] + a[i, 2] * a[j, 0],
                         a[i, 1] * a[j, 1], a[i, 1] * a[j, 2] + a[i, 2] * a[j, 1], a[i, 2] * a[j, 2]])
            rhs.append(cov[i, j])
    s = np.linalg.lstsq(np.array(rows), np.array(rhs), rcond=None)[0]
    spread = np.array([[s[0], s[1], s[2]], [s[1], s[3], s[4]], [s[2], s[4], s[5]]])
    facing = np.linalg.eigh(spread)[1][:, 0]  # the least spread: across the disc
    centre = np.linalg.lstsq(np.vstack([proj[d] for d in seen]), np.concatenate([m for m, c in seen.values()]),
                             rcond=None)[0]
    return Vector(facing).normalized(), Vector(centre)


def local(rig, name, rotation):
    joint = rig.joints[name]
    return joint.matrix_parent_inverse @ Matrix.Translation(rig.rest[name]) @ Euler(rotation).to_matrix().to_4x4()


def solve_arm(rig, pose, blade, hilt, length, previous=None):
    """Shoulder, elbow and wrist that hold the blade along `blade` from `hilt`
    (`length` long in the original)."""
    rig.pose(pose)
    creatures.plant(rig)
    bpy.context.view_layer.update()
    chest = rig.joints["arm_r"].parent.matrix_world.copy()
    best = None
    # The shoulder turns three ways: out from the side, forward and back,
    # and across the chest (the last brings the hand in under the body).
    for ax in [x / 10 for x in range(-12, 17)]:
        for az in [z / 5 for z in range(-4, 5)]:
            for ay in [y / 16 for y in range(-56, 73)]:
                arm = chest @ local(rig, "arm_r", (ax, ay, az))
                for ey in [-e / 8 for e in range(14)]:
                    elbow = arm @ local(rig, "elbow_r", (0, ey, 0))
                    miss = ((elbow @ local(rig, "weapon", (0, 0, 0))).translation - hilt).length
                    if previous:  # the same pull as the shield arm's
                        miss += 0.015 * (abs(ax - previous[0]) + abs(ay - previous[1]) + abs(az - previous[2]) +
                                         abs(ey - previous[3]))
                    if best is None or miss < best[0]:
                        best = (miss, ax, ay, az, ey, elbow)
    _, ax, ay, az, ey, elbow = best
    hand = (elbow @ local(rig, "weapon", (0, 0, 0))).translation
    # Where the hand falls short, point the blade at the original's tip, so
    # it still crosses the body where the original's does.
    aim = (hilt + blade * length - hand).normalized()
    wrist = min(((((elbow @ local(rig, "weapon", (0, math.radians(wy), wz))).to_3x3() @ Vector((1, 0, 0)))
                  .normalized() - aim).length, wy, wz)
                for wy in range(-180, 181, 3) for wz in [z / 20 for z in range(-32, 33)])
    return (ax, ay, az), (0, ey, 0), (wrist[1], wrist[2]), (hand - hilt).length, wrist[0]


def solve_shield_arm(rig, pose, facing, centre, previous=None):
    """Shoulder and elbow that put the shield at `centre`, facing `facing`
    (either way: a picture cannot tell the face from the back, so the one
    facing away from the chest is taken)."""
    rig.pose(pose)
    creatures.plant(rig)
    bpy.context.view_layer.update()
    chest = rig.joints["arm_l"].parent.matrix_world.copy()
    disc = next(o for o in bpy.data.objects if o.parent is rig.joints["elbow_l"] and o.name.startswith("Cylinder"))
    offset = Vector(disc.location)

    def place(ax, ay, az, ex, ey, ez):
        elbow = chest @ local(rig, "arm_l", (ax, ay, az)) @ local(rig, "elbow_l", (ex, ey, ez))
        return elbow @ offset, (elbow.to_3x3() @ Vector((1, 0, 0))).normalized()

    middle = chest.translation
    if (centre - middle).dot(facing) < 0:
        facing = -facing

    def cost(p):
        at, face = place(*p)
        # A little pull towards the last frame's pose keeps the arm from
        # jumping between equally good ones.
        pull = 0.015 * sum(abs(a - b) for a, b in zip(p, previous)) if previous else 0
        return (at - centre).length + 0.6 * (face - facing).length + pull

    # A coarse search for the place, then a finer one for place and facing.
    starts = []
    for ax in [x / 5 for x in range(-6, 9)]:
        for ay in [y / 8 for y in range(-36, 25)]:
            for az in [z / 5 for z in range(-4, 5)]:
                for ex in [-e / 4 for e in range(8)]:
                    starts.append(((place(ax, ay, az, ex, 0, 0)[0] - centre).length, (ax, ay, az, ex, 0, 0)))
    starts.sort()
    best = min(((cost(p), p) for p in [p for _, p in starts[:60]] + ([previous] if previous else [])),
               key=lambda c: c[0])
    for step in (0.4, 0.2, 0.1, 0.05):
        improved = True
        while improved:
            improved = False
            for k in range(6):
                for sign in (-1, 1):
                    p = list(best[1])
                    p[k] += sign * step
                    c = cost(p)
                    if c < best[0] - 1e-6:
                        best, improved = (c, tuple(p)), True
    at, face = place(*best[1])
    return best[1], (at - centre).length, (face - facing).length


def main():
    kind, folder = sys.argv[-2], Path(sys.argv[-1])
    scene = studio.reset(samples=4)
    rig = creatures.BUILDERS[kind]()
    studio.camera(scene, 200, 200, target=(0, 0, creatures.HEIGHT[kind] * 0.42))
    proj = projections(scene, rig)
    table = creatures.HEROINE_ATTACK if kind == "heroine" else creatures.HERO_ATTACK
    creatures.build_gear(rig, SHIELD)
    seen, shields = measure(folder)
    frames = len(seen)
    swords, guards, previous, held = [], [], None, None
    for f in range(frames):
        pose = creatures.keyframes(table, f / (frames - 1))
        blade, hilt, length = solve_3d(proj, seen[f])
        arm, elbow, wrist, miss, off = solve_arm(rig, pose, blade, hilt, length, held)
        held = (*arm, elbow[1])
        facing, centre = solve_shield(proj, shields[f])
        previous, shield_miss, face_off = solve_shield_arm(rig, pose, facing, centre, previous)
        ax, ay, az, ex, ey, ez = previous
        print(f"# frame {f}: hand missed by {miss:.2f}, blade by {off:.2f}; "
              f"shield missed by {shield_miss:.2f}, facing by {face_off:.2f}", file=sys.stderr)
        swords.append(f"    (({arm[0]}, {arm[1]}, {arm[2]}), (0, {elbow[1]}, 0), ({wrist[0]}, {wrist[1]})),")
        guards.append(f"    (({ax:.2f}, {ay:.2f}, {az:.2f}), ({ex:.2f}, {ey:.2f}, {ez:.2f})),")
    who = "HEROINE" if kind == "heroine" else "HERO"
    print(f"{who}_SWORD_ARM = [\n" + "\n".join(swords) + "\n]")
    print(f"{who}_SHIELD_ARM = [\n" + "\n".join(guards) + "\n]")


if __name__ == "__main__":
    main()
