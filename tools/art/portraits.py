"""Full-length pictures of the hero and the heroine for the Create New
Character screen, rendered with studio.py from the models in creatures.py.

    python tools/art/portraits.py <output folder>

(run with a Python that has the `bpy` module). Each stands at ease in
their own clothes, a long sword held point down and a round shield on the
other arm, seen from a little above eye level, and is written as hero.png
and heroine.png at twice the size ui.py uses them.
"""

import math
import sys
from pathlib import Path

import bpy
from mathutils import Euler, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import creatures  # noqa: E402
import studio  # noqa: E402

WIDTH, HEIGHT = 320, 640
GEAR = (9, 17)  # round shield, long sword
# Joint rotations: the shield arm a little forward, the sword hand low.
AT_EASE = {"arm_l": (0.30, 0, 0), "arm_r": (-0.08, 0, 0), "elbow_r": (0, 0.9, 0),
           "leg_l": (0.06, 0, 0), "leg_r": (-0.06, 0, 0)}


def render(kind, out):
    scene = studio.reset(samples=64)
    rig = creatures.BUILDERS[kind]()
    for layer in GEAR:
        creatures.build_gear(rig, layer)
    bpy.data.objects["shadow catcher"].hide_render = True
    rig.pose(AT_EASE)
    # Turned a little to the viewer's left.
    rig.joints["root"].rotation_euler = Euler((0, 0, math.radians(-25)))
    cam = bpy.data.cameras.new("portrait")
    cam.type = "ORTHO"
    cam.ortho_scale = 4.8
    obj = bpy.data.objects.new("portrait", cam)
    scene.collection.objects.link(obj)
    scene.camera = obj
    scene.render.resolution_x, scene.render.resolution_y = WIDTH, HEIGHT
    pitch = math.radians(12)
    target = Vector((0, 0, 2.15))
    obj.location = target + Vector((math.cos(pitch), 0, math.sin(pitch))) * 30
    obj.rotation_euler = (math.pi / 2 - pitch, 0, math.radians(90))
    studio.render(scene, str(out / f"{kind}.png"))


def main():
    out = Path(sys.argv[-1])
    out.mkdir(parents=True, exist_ok=True)
    for kind in ("hero", "heroine"):
        render(kind, out)


if __name__ == "__main__":
    main()
