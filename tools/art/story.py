#!/usr/bin/env python3
"""The story pictures for the open content pack.

    python3 tools/art/story.py <content pack folder>

Scenario scripts show a story page with STARTVISUAL (see docs/traps.md):
picture N is System/Common/Pattern/VisualNN, one 640 x 480 page per
pattern, and picture 0 is pattern 4 of System/Common/Pattern/Waiting.
While the game waits for a click, pattern 0 of WaitIcon steps along at
(590, 440). This script paints all of them from scratch:

- Waiting: 0 the first episode's opening page, 1 a crossed-swords emblem,
  2 the waiting arrow, 3 a LOADING plate and 4 the first episode's closing
  page, numbered as the original's Waiting.njp so the engine finds page 4;
- Visual01-06: the opening and closing pages of episodes 2 to 4 (the
  second episode's close runs over two pages, in Visual02);
- WaitIcon: the little arrow.

Each page is a dark painted scene, made here from noise, gradients and
shapes, with the episode's story over it. The story is told in our own
words: a short passage per page that tells the player the situation, the
stakes and where the hero goes next, as each of the original's pages does,
with none of its sentences or images. The text is set in Liberation
Serif Italic (SIL Open Font License 1.1). Needs numpy and Pillow; pages are
saved with 256 colours, like the original's.
"""

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
from effects import bolt, noise, save_png  # noqa: E402

HEADER = "# Drawn by tools/art/story.py (our own art, see content/README.md).\n"
W, H = 640, 480

FONT_DIRS = ["/usr/share/fonts/truetype/liberation", "/usr/share/fonts/liberation",
             "/usr/share/fonts/truetype/liberation2"]


def font(size, bold=False):
    name = "LiberationSerif-BoldItalic.ttf" if bold else "LiberationSerif-Italic.ttf"
    for d in FONT_DIRS:
        if Path(d, name).exists():
            return ImageFont.truetype(str(Path(d, name)), size)
    sys.exit(f"story.py needs {name} (the Liberation fonts)")


# --- the story, page by page -------------------------------------------------
# A page is its heading and paragraphs. Our own wording throughout.

EPISODE1_OPEN = ("Episode 1", [
    "Gargoyles have been sighted along the frontier, and the sellswords sent against them are not "
    "coming back.",
    "They are a new breed of demon, far deadlier than the goblins and golems you have hunted until now.",
    "You are a hired blade with a sword and little else. Head for the fighting, find out what these "
    "creatures are, and live long enough to collect your pay.",
])

EPISODE1_CLOSE = ("Epilogue", [
    "The gargoyles are dead, but the war that bred them goes on.",
    "Humans and demons are locked in a struggle neither side will give up, and every victory only "
    "opens the way to the next battle.",
    "You have grown stronger, and restless with it. A new road lies ahead.",
])

EPISODE2_OPEN = ("Episode 2", [
    "Losmans Castle, the keystone of the central front, is close to falling to the demon siege.",
    "The allied army is planning a surprise strike: rangers sent through the old mine tunnel that "
    "runs behind the enemy fort.",
    "Deserters from the demon ranks have made the mine country a bandit stronghold, and no one has "
    "scouted the tunnel yet.",
    "Your contract is to clear the mine and drive out the bandits so the rangers can pass. Beyond "
    "it, the mountains wait.",
])

EPISODE2_CLOSE = ("Epilogue", [
    "The Ancient Dragon is slain.",
    "It had lain sealed away for more than ten years, kept alive only by its own magic, its old "
    "wounds never closing.",
    "In the end, the blow that killed it was less a victory than a release.",
])

EPISODE2_CLOSE_2 = ("", [
    "The fight leaves a hard question behind: are the demons truly the enemy, or are humans and "
    "demons alike pieces in some larger design?",
    "The church forbids even asking.",
    "Legend names Hiperion, a land no traveler has ever reached, as the place where the answer lies.",
])

EPISODE3_OPEN = ("Episode 3", [
    "Since their defeat at Losmans, the demons have pulled back and gone quiet.",
    "In these dry borderlands the church is spreading fast, and a great demon fort nearby has woken "
    "up as it does.",
    "Treasure hunters already have their eyes on the fort's forbidden grounds.",
    "With the enemy lying low, this is the moment to go in, but what waits inside will test "
    "everything you have learned.",
])

EPISODE3_CLOSE = ("Epilogue", [
    "Archbishop Jurg, driven mad by the loss of his son, gave himself to the demons for revenge, "
    "and now he has fallen.",
    "Saint Knight Gawl turned against the Noil church because he doubted its teachings.",
    "The Noil faith, the White Angel Altecia and the Ancient Technology all seem bound up in one "
    "secret.",
    "The trail leads west, to Hiperion, the forbidden land the Noils sealed.",
])

EPISODE4_OPEN = ("Episode 4", [
    "The road has led here, and there is no turning back.",
    "You fight on, pushed by an anger that neither rest nor fate can quiet.",
    "Powerful enemies stand between you and the end of this war, and the hardest battles are still "
    "ahead.",
])

EPISODE4_CLOSE = ("Epilogue", [
    "The Devil is defeated, yet the world is no kinder for it: humans still fight demons, and still "
    "fight each other.",
    "Even so, ordinary people go on living and find what happiness they can.",
    "Swords and spells won this war, but the strength that matters most is the one every person "
    "carries.",
])


# --- painting -----------------------------------------------------------------

def ramp(t, stops):
    """Colours for 0..1 along a list of RGB stops."""
    t = np.clip(t, 0, 1)
    stops = np.asarray(stops, np.float32)
    x = t * (len(stops) - 1)
    i = np.clip(x.astype(int), 0, len(stops) - 2)
    f = (x - i)[..., None]
    return stops[i] * (1 - f) + stops[i + 1] * f


def fbm(seed, cell=60, octaves=5):
    return noise(W, H, cell, seed, octaves)


def grid():
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float32)
    return xs / W, ys / H


def shape_mask(draw_fn, blur=1.5):
    """A soft 0..1 mask of whatever draw_fn(draw) fills in white."""
    im = Image.new("L", (W, H))
    draw_fn(ImageDraw.Draw(im))
    if blur:
        im = im.filter(ImageFilter.GaussianBlur(blur))
    return np.asarray(im, np.float32) / 255


def lay(c, mask, colour, k=1.0):
    """Paints `colour` (RGB or an image) over `c` through `mask`."""
    m = (np.clip(mask, 0, 1) * k)[..., None]
    return c * (1 - m) + np.asarray(colour, np.float32) * m


def brushwork(c, seed, strength=0.18):
    """Streaks of dabbed paint and a coarse canvas grain over the picture."""
    rng = np.random.default_rng(seed)
    streak = Image.fromarray((noise(W, H, 3, seed + 1, 2) * 255).astype(np.uint8))
    streak = np.asarray(streak.filter(ImageFilter.BoxBlur(0)).resize((W // 4, H)).resize((W, H), Image.BICUBIC),
                        np.float32) / 255
    angle = rng.uniform(-0.6, 0.6)
    streak = np.asarray(Image.fromarray((streak * 255).astype(np.uint8)).rotate(math.degrees(angle), Image.BICUBIC,
                                                                               expand=False, fillcolor=128),
                        np.float32) / 255
    grain = noise(W, H, 1.5, seed + 2, 1)
    c = c * (1 + strength * (streak - 0.5) * 1.2 + 0.06 * (grain - 0.5))[..., None]
    im = Image.fromarray(np.clip(c, 0, 255).astype(np.uint8))
    im = im.filter(ImageFilter.ModeFilter(3))  # flattens it into dabs of paint
    return np.asarray(im, np.float32)


def vignette(c, k=0.6, centre=0.35):
    """Darkens the edges, and the middle a little, where the words go."""
    x, y = grid()
    r = np.sqrt(((x - 0.5) / 0.62) ** 2 + ((y - 0.5) / 0.62) ** 2)
    edge = 1 - k * np.clip(r - 0.35, 0, 1) ** 1.4
    middle = 1 - centre * np.exp(-((x - 0.5) / 0.38) ** 2 - ((y - 0.5) / 0.48) ** 2)
    return c * (edge * middle)[..., None]


def clouds(seed, stops, cell=110, sharp=1.3, sky=None):
    x, y = grid()
    n = fbm(seed, cell, 5)
    base = sky if sky is not None else y
    return ramp(np.clip((n - 0.5) * sharp + base * 0.8 + 0.1, 0, 1), stops)


def ridge(seed, base, rough, jag=0.0):
    """A skyline: a mask of everything below a wandering line `base` of the
    way down the page."""
    rng = np.random.default_rng(seed)
    xs = np.linspace(0, 1, 160)
    line = np.full_like(xs, base)
    for k in range(1, 7):
        line += rough / k * np.sin(xs * math.pi * (k + rng.uniform(0, 1)) * 2 + rng.uniform(0, 6.3))
    line += jag * rng.normal(0, 1, len(xs))
    pts = [(x * W, y * H) for x, y in zip(xs, line)] + [(W, H), (0, H)]
    return shape_mask(lambda d: d.polygon(pts, fill=255), 1.2)


def gargoyle(draw, x, y, size, fill=255):
    """A winged fiend in flight, centred on (x, y): a hunched body, a horned
    head and two bat wings spread with their fingers up."""
    def at(px, py):
        return (x + px * size, y + py * size)
    for side in (-1, 1):
        shoulder = (0.12 * side, -0.05)
        tips = [(1.0 * side, 0.15), (0.95 * side, -0.35), (0.72 * side, -0.7), (0.38 * side, -0.85)]
        pts = [at(*shoulder), at(*tips[0])]
        for (ax, ay), (bx, by) in zip(tips, tips[1:]):
            mx, my = (ax + bx) / 2, (ay + by) / 2
            pts.append(at(mx + (shoulder[0] - mx) * 0.22, my + (shoulder[1] - my) * 0.22))
            pts.append(at(bx, by))
        pts.append(at(0.1 * side, -0.25))
        draw.polygon(pts, fill=fill)
        for tx, ty in tips:
            draw.line([at(*shoulder), at(tx, ty)], fill=fill, width=max(2, int(size * 0.03)))
        draw.polygon([at(0.05 * side, 0.25), at(0.18 * side, 0.55), at(0.08 * side, 0.5)], fill=fill)  # legs
        draw.polygon([at(0.05 * side, -0.22), at(0.16 * side, -0.4), at(0.1 * side, -0.2)], fill=fill)  # horns
    draw.ellipse([*at(-0.16, -0.12), *at(0.16, 0.32)], fill=fill)  # the body
    draw.ellipse([*at(-0.09, -0.27), *at(0.09, -0.08)], fill=fill)  # the head
    draw.line([at(0, 0.3), at(-0.2, 0.62), at(-0.36, 0.6)], fill=fill, width=max(2, int(size * 0.025)))  # tail


def pine(draw, x, y, h, fill):
    """A fir tree standing on (x, y), `h` tall."""
    tiers = 6
    for i in range(tiers):
        t0 = i / tiers
        w = h * 0.28 * (1 - t0 * 0.75)
        top = y - h * (t0 + 0.32)
        bottom = y - h * t0 * 0.95
        draw.polygon([(x - w, bottom), (x + w, bottom), (x, top)], fill=fill)
    draw.rectangle([x - h * 0.02, y - h * 0.1, x + h * 0.02, y], fill=fill)


def tower(draw, x, y, w, h, fill, spire=True):
    draw.rectangle([x - w / 2, y - h, x + w / 2, y], fill=fill)
    for i in range(3):  # battlements
        cx = x - w / 2 + (i + 0.5) * w / 3
        draw.rectangle([cx - w / 10, y - h - w / 6, cx + w / 10, y - h], fill=fill)
    if spire:
        draw.polygon([(x - w * 0.55, y - h), (x + w * 0.55, y - h), (x, y - h - w * 1.6)], fill=fill)


# --- the scenes ---------------------------------------------------------------

def storm(seed):
    """Episode 1 opens: a storm over a broken battlefield, a winged shape in
    the clouds and soldiers' weapons against the light."""
    rng = np.random.default_rng(seed)
    x, y = grid()
    c = clouds(seed, [(22, 22, 26), (58, 58, 64), (110, 108, 112), (160, 158, 160)], 120, 1.6, sky=1 - y * 0.9)
    pts = bolt(rng, 470, 0, 420, 300, 7, 0.3)
    glow = shape_mask(lambda d: d.line(pts, fill=255, width=3), 6)
    core = shape_mask(lambda d: d.line(pts, fill=255, width=2), 0.6)
    c = lay(c, glow, (200, 205, 220), 0.7)
    c = lay(c, core, (245, 245, 250))
    c = lay(c, shape_mask(lambda d: gargoyle(d, 120, 150, 120), 1.5), (14, 12, 14), 0.92)
    c = lay(c, shape_mask(lambda d: gargoyle(d, 250, 70, 40), 1.2), (40, 38, 42), 0.8)
    ground = ridge(seed + 1, 0.8, 0.03, 0.002)
    c = lay(c, ground, ramp(fbm(seed + 2, 30) * 0.8, [(18, 18, 20), (60, 58, 56)]))

    def spears(d):
        for i in range(14):
            bx = rng.uniform(340, 640)
            by = rng.uniform(380, 470)
            a = math.radians(rng.uniform(-115, -65))
            ln = rng.uniform(80, 170)
            d.line([(bx, by), (bx + math.cos(a) * ln, by + math.sin(a) * ln)], fill=255, width=3)
    c = lay(c, shape_mask(spears, 0.8), (16, 15, 16), 0.9)
    return c


def forest(seed):
    """Episode 1 closes: a misty path through old trees toward a ruined keep,
    in green-grey wash."""
    rng = np.random.default_rng(seed)
    x, y = grid()
    c = clouds(seed, [(28, 36, 30), (70, 86, 72), (130, 146, 128), (178, 188, 170)], 90, 1.2, sky=1 - y)
    c = lay(c, shape_mask(lambda d: tower(d, 520, 300, 60, 150, 255), 2), (64, 78, 66), 0.85)
    c = lay(c, shape_mask(lambda d: tower(d, 590, 320, 40, 110, 255), 2), (58, 72, 60), 0.85)
    path = shape_mask(lambda d: d.polygon([(250, 480), (560, 480), (470, 330), (430, 330)], fill=255), 10)
    ground = ridge(seed + 1, 0.68, 0.02)
    c = lay(c, ground, ramp(fbm(seed + 3, 25), [(30, 40, 32), (66, 82, 64)]))
    c = lay(c, path, (120, 134, 112), 0.7)

    def trunks(d):
        for i, (tx, tw) in enumerate([(30, 70), (130, 38), (600, 50), (200, 22)]):
            lean = rng.uniform(-30, 30)
            d.polygon([(tx - tw / 2, 480), (tx + tw / 2, 480), (tx + tw * 0.3 + lean, 0), (tx - tw * 0.3 + lean, 0)],
                      fill=255)
            for _ in range(3):  # boughs
                by = rng.uniform(40, 220)
                d.line([(tx + lean * (1 - by / 480), by), (tx + rng.choice([-1, 1]) * rng.uniform(60, 130), by - 50)],
                       fill=255, width=int(tw / 5) + 2)
    c = lay(c, shape_mask(trunks, 1.2), (20, 28, 22), 0.92)
    leaves = np.clip((fbm(seed + 4, 18, 3) - 0.5) * 4 + (0.25 - y) * 3, 0, 1)
    c = lay(c, leaves, (26, 36, 28), 0.85)
    # Pen hatching over the wash, as in an ink drawing.
    hatch = Image.new("L", (W, H))
    hd = ImageDraw.Draw(hatch)
    for _ in range(900):
        hx, hy = rng.uniform(0, W), rng.uniform(0, H)
        ln = rng.uniform(4, 12)
        hd.line([(hx, hy), (hx + ln, hy - ln * 0.6)], fill=int(rng.uniform(40, 110)))
    c = c * (1 - np.asarray(hatch, np.float32)[..., None] / 255 * 0.35)
    mist = np.clip(fbm(seed + 5, 80) - 0.45, 0, 1) * np.exp(-((y - 0.62) / 0.12) ** 2)
    return lay(c, mist * 1.4, (170, 182, 164), 0.6)


def dust(seed):
    """Episode 2 opens: a host on the march under a sky of dust, pikes and
    banners against it and a fortress on the skyline."""
    rng = np.random.default_rng(seed)
    x, y = grid()
    c = clouds(seed, [(60, 46, 34), (120, 96, 72), (176, 150, 118), (214, 196, 168)], 70, 1.8, sky=0.7 - y * 0.4)
    c = lay(c, shape_mask(lambda d: [tower(d, 90 + i * 40, 250, 34, 70 + 30 * (i % 2), 255, spire=False)
                                     for i in range(4)], 3), (96, 78, 60), 0.7)
    c = lay(c, ridge(seed + 1, 0.55, 0.02), (88, 70, 52), 0.8)

    def host(d):
        for i in range(60):
            bx = rng.uniform(-20, 660)
            by = rng.uniform(300, 520)
            s = (by - 260) / 220
            d.ellipse([bx - 14 * s, by - 60 * s, bx + 14 * s, by], fill=255)  # a body
            d.ellipse([bx - 7 * s, by - 76 * s, bx + 7 * s, by - 58 * s], fill=255)  # a helmet
            a = math.radians(rng.uniform(-110, -60))
            d.line([(bx, by - 30 * s), (bx + math.cos(a) * 140 * s, by - 30 * s + math.sin(a) * 140 * s)], fill=255,
                   width=max(1, int(3 * s)))
            if i % 9 == 0:  # a banner
                tx, ty = bx + math.cos(a) * 140 * s, by - 30 * s + math.sin(a) * 140 * s
                d.polygon([(tx, ty), (tx + 40 * s, ty + 8 * s), (tx, ty + 22 * s)], fill=255)
    c = lay(c, shape_mask(host, 1.0), ramp(fbm(seed + 2, 12), [(30, 22, 16), (72, 56, 42)]), 0.92)
    haze = np.clip(fbm(seed + 3, 50) - 0.3, 0, 1) * (0.4 + 0.6 * y)
    return lay(c, haze, (196, 170, 136), 0.55)


def lair(seed, page):
    """Episode 2 closes: the dragon's lair, raw red rock veined like flesh,
    a heap of scales and bone below."""
    rng = np.random.default_rng(seed)
    x, y = grid()
    rock = fbm(seed, 40)
    c = ramp(rock * 1.2 - 0.1, [(30, 18, 16), (78, 46, 38), (128, 88, 72), (170, 140, 120)])

    def veins(d):
        for _ in range(16):
            x0, y0 = rng.uniform(0, W), rng.uniform(0, H * 0.6)
            pts = bolt(rng, x0, y0, x0 + rng.normal(0, 160), y0 + rng.uniform(60, 260), 5, 0.5)
            d.line(pts, fill=255, width=int(rng.uniform(1, 4)))
    c = lay(c, shape_mask(veins, 1.4), (110, 14, 12), 0.75)
    c = lay(c, shape_mask(veins, 5), (90, 10, 10), 0.35)

    def scales(d):
        for row in range(7):
            yy = 360 + row * 18
            for col in range(-1, 24):
                xx = col * 30 + (row % 2) * 15 + rng.normal(0, 2)
                d.arc([xx - 16, yy - 14, xx + 16, yy + 16], 200, 340, fill=255, width=3)
    sc = shape_mask(scales, 0.8) * np.clip((y - 0.68) * 5, 0, 1)
    c = lay(c, sc, (190, 170, 150), 0.6)
    bones = np.clip((fbm(seed + 7, 26, 3) - 0.62) * 6, 0, 1) * np.clip((y - 0.55) * 3, 0, 1)
    c = lay(c, bones, (200, 190, 176), 0.7)
    if page == 1:  # a great claw reaching in from below
        def claw(d):
            for i in range(3):
                bx = 120 + i * 50
                d.polygon([(bx, 480), (bx + 36, 480), (bx + 70 + i * 10, 360 - i * 20), (bx + 40, 400)], fill=255)
        c = lay(c, shape_mask(claw, 2), (60, 50, 46), 0.85)
    return c


def church(seed):
    """Episode 3 opens: a dry land at dusk, a chapel's spire on one side and
    a demon fort smouldering on the hill on the other."""
    rng = np.random.default_rng(seed)
    x, y = grid()
    c = clouds(seed, [(40, 32, 30), (92, 80, 72), (150, 136, 120), (196, 184, 166)], 100, 1.4, sky=0.9 - y * 0.6)
    c = lay(c, ridge(seed + 1, 0.62, 0.05), (70, 58, 48), 0.9)

    def chapel(d):
        d.rectangle([40, 250, 150, 380], fill=255)
        d.polygon([(30, 250), (160, 250), (95, 190)], fill=255)
        d.rectangle([75, 120, 115, 250], fill=255)
        d.polygon([(70, 120), (120, 120), (95, 40)], fill=255)
        d.line([(95, 40), (95, 15)], fill=255, width=3)
        d.line([(84, 26), (106, 26)], fill=255, width=3)
    c = lay(c, shape_mask(chapel, 1.2), (34, 28, 26), 0.92)

    def fort(d):
        for i in range(5):
            tower(d, 430 + i * 38, 300, 30, 50 + 25 * ((i * 7) % 3), 255, spire=i % 2 == 0)
        d.rectangle([420, 260, 620, 300], fill=255)
    c = lay(c, shape_mask(fort, 1.5), (40, 30, 28), 0.9)
    heat_ = np.exp(-((x - 0.82) / 0.14) ** 2 - ((y - 0.58) / 0.08) ** 2)
    c = lay(c, heat_, (200, 90, 40), 0.45)
    c = lay(c, np.clip(fbm(seed + 2, 20) - 0.55, 0, 1) * 3 * (y > 0.65), (110, 92, 70), 0.6)
    return c


def gilded(seed):
    """Episode 3 closes: a temple's gilded tracery and a crimson mosaic,
    twisted and cracked."""
    rng = np.random.default_rng(seed)
    x, y = grid()
    # Mosaic: each pixel takes the colour of its nearest tile seed.
    pts = rng.uniform(0, 1, (900, 2)) * [W, H]
    cols = ramp(rng.uniform(0, 1, 900), [(50, 10, 12), (110, 30, 26), (150, 70, 40), (90, 60, 30)])
    small = Image.new("RGB", (W // 2, H // 2))
    sx, sy = np.mgrid[0:H // 2, 0:W // 2]
    best = np.full((H // 2, W // 2), 1e9)
    idx = np.zeros((H // 2, W // 2), int)
    for i, (px, py) in enumerate(pts):
        d = (sy * 2 - px) ** 2 + (sx * 2 - py) ** 2
        better = d < best
        best[better] = d[better]
        idx[better] = i
    small = np.asarray(Image.fromarray(cols[idx].astype(np.uint8)).resize((W, H), Image.NEAREST), np.float32)
    c = small * (0.7 + 0.5 * fbm(seed + 1, 30)[..., None])

    def tracery(d):
        for _ in range(26):
            x0, y0 = rng.uniform(-40, W + 40), rng.uniform(-40, H + 40)
            ang = rng.uniform(0, 2 * math.pi)
            ln = rng.uniform(120, 320)
            p = bolt(rng, x0, y0, x0 + math.cos(ang) * ln, y0 + math.sin(ang) * ln, 5, 0.35)
            d.line(p, fill=255, width=int(rng.uniform(3, 9)))
    m = shape_mask(tracery, 1.2)
    gold = ramp(fbm(seed + 2, 6, 2), [(90, 66, 24), (190, 150, 70), (250, 226, 150)])
    c = lay(c, shape_mask(tracery, 6), (60, 40, 10), 0.5)
    c = lay(c, m, gold, 0.95)
    return c


def blaze(seed):
    """Episode 4 opens: a warrior black against a wall of fire."""
    rng = np.random.default_rng(seed)
    x, y = grid()
    n = fbm(seed, 40)
    tongues = np.asarray(Image.fromarray((fbm(seed + 1, 8, 3) * 255).astype(np.uint8)).resize((W // 6, H))
                         .resize((W, H), Image.BICUBIC), np.float32) / 255
    t = np.clip(n * 0.6 + tongues * 0.6 + (y - 0.3) * 0.5, 0, 1)
    c = ramp(t, [(40, 8, 4), (130, 30, 8), (220, 90, 20), (250, 170, 60), (255, 236, 190)])

    def figure(d):
        cx, cy = 330, 470
        d.polygon([(cx - 90, cy), (cx + 90, cy), (cx + 60, cy - 170), (cx - 60, cy - 170)], fill=255)  # cloak
        d.ellipse([cx - 70, cy - 240, cx + 70, cy - 150], fill=255)  # shoulders
        d.ellipse([cx - 28, cy - 290, cx + 28, cy - 225], fill=255)  # helm
        d.polygon([(cx - 22, cy - 280), (cx - 60, cy - 330), (cx - 10, cy - 290)], fill=255)  # horns
        d.polygon([(cx + 22, cy - 280), (cx + 60, cy - 330), (cx + 10, cy - 290)], fill=255)
        d.polygon([(cx - 70, cy - 220), (cx - 40, cy - 190), (cx - 150, cy - 320), (cx - 172, cy - 306)], fill=255)
        d.polygon([(cx + 70, cy - 220), (cx + 40, cy - 190), (cx + 142, cy - 330), (cx + 166, cy - 318)], fill=255)
        d.ellipse([cx - 176, cy - 336, cx - 148, cy - 308], fill=255)  # fists
        d.ellipse([cx + 140, cy - 344, cx + 168, cy - 316], fill=255)
        d.polygon([(cx + 150, cy - 336), (cx + 162, cy - 340), (cx + 236, cy - 468), (cx + 228, cy - 470)],
                  fill=255)  # a blade held high
        d.line([(cx + 136, cy - 318), (cx + 176, cy - 340)], fill=255, width=6)  # its guard
    fig = shape_mask(figure, 2.5)
    c = lay(c, fig, ramp(fbm(seed + 2, 10), [(14, 6, 4), (60, 16, 8)]), 0.95)
    rim = np.clip(shape_mask(figure, 7) - fig, 0, 1)
    c = lay(c, rim, (255, 180, 80), 0.5)
    embers = np.clip((noise(W, H, 2.5, seed + 3, 1) - 0.93) * 12, 0, 1)
    return lay(c, embers, (255, 220, 140), 0.6)


def snowfield(seed):
    """Episode 4 closes: a pale winter evening, dark firs and a village on
    the snow."""
    rng = np.random.default_rng(seed)
    x, y = grid()
    c = clouds(seed, [(60, 64, 80), (130, 128, 140), (200, 190, 180), (236, 226, 206)], 120, 1.0,
               sky=0.9 - y * 1.3)

    def firs(d, far):
        for i in range(40 if far else 18):
            fx = rng.uniform(-20, 660)
            fy = 250 + rng.uniform(-10, 10) if far else 300 + rng.uniform(-20, 20)
            pine(d, fx, fy, rng.uniform(90, 160) * (0.7 if far else 1.3), 255)
    c = lay(c, shape_mask(lambda d: firs(d, True), 2.5), (70, 74, 88), 0.75)
    c = lay(c, shape_mask(lambda d: firs(d, False), 1.2), (26, 28, 36), 0.9)
    snow = ridge(seed + 1, 0.62, 0.01)
    c = lay(c, snow, ramp(fbm(seed + 2, 30) + (y - 0.6), [(90, 96, 120), (160, 166, 186), (200, 204, 216)]))

    def houses(d):
        for hx in (90, 470, 560):
            d.rectangle([hx, 330, hx + 50, 365], fill=255)
            d.polygon([(hx - 6, 330), (hx + 56, 330), (hx + 25, 305)], fill=255)
    c = lay(c, shape_mask(houses, 1), (50, 40, 36), 0.85)
    lit = shape_mask(lambda d: [d.rectangle([hx + 18, 342, hx + 28, 352], fill=255) for hx in (90, 470, 560)], 2)
    c = lay(c, lit, (255, 200, 110), 0.9)
    return c


# --- the words ------------------------------------------------------------------

def wrap(draw, text, fnt, width):
    lines, line = [], ""
    for word in text.split():
        trial = f"{line} {word}".strip()
        if draw.textlength(trial, font=fnt) <= width:
            line = trial
        else:
            lines.append(line)
            line = word
    if line:
        lines.append(line)
    return lines


def words(c, page):
    """Sets a page's heading and paragraphs, centred, cream on a dark halo."""
    title, paragraphs = page
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    for size in range(20, 13, -1):  # the biggest type that fits the page
        body = font(size)
        head = font(size + 5)
        lh = int(size * 1.18)
        blocks = [wrap(probe, p, body, 560) for p in paragraphs]
        height = (int((size + 5) * 1.5) if title else 0) + sum(len(b) * lh for b in blocks) \
            + (len(blocks) - 1) * lh // 2
        if height <= 440:
            break
    ink = Image.new("L", (W, H))
    d = ImageDraw.Draw(ink)
    top = (H - height) // 2
    yy = top
    if title:
        d.text((W / 2, yy), title, font=head, fill=255, anchor="ma")
        yy += int((size + 5) * 1.5)
    for b in blocks:
        for line in b:
            d.text((W / 2, yy), line, font=body, fill=255, anchor="ma")
            yy += lh
        yy += lh // 2
    m = np.asarray(ink, np.float32) / 255
    halo = np.asarray(ink.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(3)), np.float32) / 255
    c = lay(c, np.clip(halo * 1.6, 0, 1), (8, 6, 6), 0.8)
    return lay(c, m, (246, 238, 214))


def page(scene, seed, text, *args):
    c = scene(seed, *args)
    c = brushwork(c, seed + 50)
    c = vignette(c)
    c = words(c, text)
    return Image.fromarray(np.clip(c, 0, 255).astype(np.uint8)).convert("RGBA")


# --- the little pieces of Waiting and WaitIcon ------------------------------------

def arrow_icon():
    """A bronze arrowhead pointing right, bevelled, 22 x 22."""
    s = 8
    im = Image.new("RGBA", (22 * s, 22 * s))
    d = ImageDraw.Draw(im)
    d.polygon([(4 * s, 3 * s), (19 * s, 11 * s), (4 * s, 19 * s), (8 * s, 11 * s)], fill=(46, 26, 12, 255))
    d.polygon([(5 * s, 4.5 * s), (17 * s, 11 * s), (8.5 * s, 11 * s)], fill=(236, 186, 104, 255))
    d.polygon([(5 * s, 17.5 * s), (17 * s, 11 * s), (8.5 * s, 11 * s)], fill=(150, 90, 40, 255))
    im = im.resize((22, 22), Image.LANCZOS)
    a = np.asarray(im).copy()
    a[..., 3] = np.where(a[..., 3] > 100, 255, 0)
    return Image.fromarray(a, "RGBA")


def swords_emblem():
    """Two pale swords crossed, 240 x 240, on transparency."""
    s = 2
    im = Image.new("RGBA", (240 * s, 240 * s))
    d = ImageDraw.Draw(im)
    for (hx, hy), (tx, ty) in (((52, 206), (196, 30)), ((188, 206), (44, 30))):
        ln = math.hypot(tx - hx, ty - hy)
        ux, uy = (tx - hx) / ln, (ty - hy) / ln

        def p(u, v, hx=hx, hy=hy, ux=ux, uy=uy):  # u along the sword from the pommel, v across it
            return ((hx + ux * u - uy * v) * s, (hy + uy * u + ux * v) * s)
        d.polygon([p(40, -8), p(ln - 22, -6), p(ln, 0), p(ln - 22, 6), p(40, 8)], fill=(214, 206, 186, 255))
        d.polygon([p(40, -1), p(ln - 22, -1), p(ln, 0), p(ln - 22, 1), p(40, 1)], fill=(250, 246, 232, 255))
        d.polygon([p(32, -26), p(40, -26), p(40, 26), p(32, 26)], fill=(150, 120, 70, 255))  # the guard
        d.polygon([p(6, -4), p(32, -4), p(32, 4), p(6, 4)], fill=(80, 56, 36, 255))  # the grip
        px, py = p(4, 0)
        d.ellipse([px - 7 * s, py - 7 * s, px + 7 * s, py + 7 * s], fill=(170, 136, 80, 255))  # the pommel
    return im.resize((240, 240), Image.LANCZOS)


def loading_plate():
    im = Image.new("RGBA", (64, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.polygon([(0, 18), (52, 18), (63, 31), (0, 31)], fill=(112, 44, 26, 255))
    d.line([(0, 18), (52, 18)], fill=(214, 150, 96, 255))
    d.line([(0, 31), (63, 31)], fill=(46, 18, 10, 255))
    for i in range(2, 50, 4):  # hammered grain
        d.line([(i, 21), (i + 3, 28)], fill=(132, 58, 34, 255))
    d.line([(52, 18), (63, 31)], fill=(196, 150, 90, 255), width=2)
    d.text((32, 1), "LOADING", font=font(13, bold=True), fill=(226, 206, 170, 255), anchor="ma")
    return im


def save_paletted(sheet, small, path):
    """256 colours, index 0 see-through, as effects.save_png; the small
    pictures are weighted up when the palette is chosen, so the pages'
    many colours do not crowd out the arrow's and the plate's."""
    if not small:
        save_png(sheet, path)
        return
    arr = np.asarray(sheet)
    rgb = Image.fromarray(np.ascontiguousarray(arr[..., :3]))
    extra = []
    for im in small:
        a = np.asarray(im.convert("RGBA"))
        px = a[a[..., 3] > 0][:, :3]
        extra.append(np.tile(px, (max(1, 60000 // max(len(px), 1)), 1)))
    sample = np.concatenate([arr[..., :3].reshape(-1, 3)] + extra)
    pal_im = Image.fromarray(sample.reshape(1, -1, 3)).quantize(255, method=Image.Quantize.MEDIANCUT,
                                                                  dither=Image.Dither.NONE)
    q = rgb.quantize(palette=pal_im, dither=Image.Dither.NONE)
    idx = np.asarray(q).astype(np.uint8) + 1
    idx[arr[..., 3] == 0] = 0
    pal = [0, 0, 0] + pal_im.getpalette()[:255 * 3]
    out = Image.fromarray(idx, "P")
    out.putpalette(pal + [0] * (768 - len(pal)))
    out.save(path, transparency=0, optimize=True)


def write_bank(folder, name, frames, comment):
    """One PNG and .sheet: frames (image, screen x, screen y) stacked."""
    width = max(im.width for im, _, _ in frames)
    height = sum(im.height + 1 for im, _, _ in frames)
    sheet = Image.new("RGBA", (width, height))
    lines = [HEADER, f"# {comment}\n", f"image={name}.png\n"]
    y = 0
    for im, x0, y0 in frames:
        sheet.paste(im, (0, y))
        lines.append(f"frame=0,{y},{im.width},{im.height},{-x0},{-y0}\n")
        y += im.height + 1
    folder.mkdir(parents=True, exist_ok=True)
    save_paletted(sheet, [im for im, _, _ in frames if im.width * im.height < 100000], folder / f"{name}.png")
    (folder / f"{name}.sheet").write_text("".join(lines))
    print(f"{name}: {len(frames)} frames")


VISUALS = {
    1: [(dust, 201, EPISODE2_OPEN)],
    2: [(lair, 202, EPISODE2_CLOSE, 0), (lair, 203, EPISODE2_CLOSE_2, 1)],
    3: [(church, 301, EPISODE3_OPEN)],
    4: [(gilded, 302, EPISODE3_CLOSE)],
    5: [(blaze, 401, EPISODE4_OPEN)],
    6: [(snowfield, 402, EPISODE4_CLOSE)],
}


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = Path(sys.argv[1]) / "System/Common/Pattern"
    icon = arrow_icon()
    write_bank(out, "Waiting", [
        (page(storm, 101, EPISODE1_OPEN), 0, 0),
        (swords_emblem(), 200, 120),
        (icon, 0, 0),
        (loading_plate(), 0, 0),
        (page(forest, 102, EPISODE1_CLOSE), 0, 0),
    ], "0 and 4: the first episode's opening and closing pages; 1 an emblem, 2 an arrow, 3 a LOADING plate.")
    write_bank(out, "WaitIcon", [(icon, 0, 0)], "The arrow that steps along while a story page waits for a click.")
    for number, pages in VISUALS.items():
        write_bank(out, f"Visual{number:02d}", [(page(*p), 0, 0) for p in pages],
                   "A story page per frame, 640 x 480, shown by STARTVISUAL.")


if __name__ == "__main__":
    main()
