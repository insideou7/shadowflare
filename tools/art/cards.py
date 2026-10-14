#!/usr/bin/env python3
"""The card game and the Change Color window for the open content pack.

    python3 tools/art/cards.py <content pack folder>

Draws, in the style of the original but from scratch (nothing traced or
sampled from it):

- System/Game/Pattern/Card: the card game's pictures, numbered as the
  original's Card.Njp so the engine (src/game/card_game.cpp) draws them
  the same way: the 52 cards (spades, clubs, diamonds, hearts, ace to
  king, on old parchment in a bronze edge, the court cards with a crown,
  a coronet or a helm over the suit), the joker, the back (maroon with a
  bronze knot), the card's shadow, the dealer (a small hooded figure) and
  his shadow, the BLACK JACK and BREAK plates, the HIT and STAY buttons,
  the WINNER and LOSER plates, the button's and the plate's shadows, the
  table (dark baize with a carved bronze knot), DRAW, the buttons again,
  and last the carved frame round the table.
- System/Game/Pattern/ChangeColor: the Change Color window at (160, 144),
  320 x 192 of the same carved stone, with the picture's sunken stage, the
  Weapon, Shield and Armor tabs, the 4 x 4 swatch sockets, Restore, OK
  and CANCEL; then Restore pushed, OK and CANCEL lit, each tab chosen and
  the swatch the engine tints with each dye, where
  src/game/change_color.cpp (after ShadowFlare.exe) puts them.

Needs numpy and Pillow.
"""

import math
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

import magic
import shrink
import ui

HEADER = "# Drawn by tools/art/cards.py (our own art, see content/README.md).\n"

CW, CH = 63, 98  # a card
PARCHMENT = np.array([214, 196, 150])
INK = (24, 20, 18, 255)
RED = (150, 26, 22, 255)
GOLD = (220, 196, 132, 255)


def noise(shape, scale, seed):
    rng = np.random.default_rng(seed)
    h, w = shape
    grid = rng.random((h // scale + 2, w // scale + 2))
    big = Image.fromarray((grid * 255).astype(np.uint8)).resize(((w // scale + 2) * scale, (h // scale + 2) * scale), Image.BICUBIC)
    return np.asarray(big, dtype=float)[:h, :w] / 255


def card_blank(seed):
    """Parchment, mottled and darker towards its ragged bronze edge."""
    yy, xx = np.mgrid[0:CH, 0:CW].astype(float)
    mottle = 0.55 * noise((CH, CW), 12, seed) + 0.3 * noise((CH, CW), 4, seed + 1) + 0.15
    edge = np.minimum.reduce([xx, CW - 1 - xx, yy, CH - 1 - yy])
    rag = edge + 1.6 * (noise((CH, CW), 3, seed + 2) - 0.5)
    shade = 0.78 + 0.3 * mottle - 0.25 * np.clip(1 - edge / 10, 0, 1)
    rgb = PARCHMENT * shade[..., None]
    border = (rag > 1.0) & (rag < 3.2)
    rgb[border] = ui.BRONZE * (0.55 + 0.25 * mottle[border, None])
    alpha = np.where(rag > 0.3, 255, 0)
    img = np.dstack([np.clip(rgb, 0, 255), alpha]).astype(np.uint8)
    return Image.fromarray(img, "RGBA")


def suit_shape(draw, suit, cx, cy, s, colour):
    """A pip `s` pixels tall centred on (cx, cy): 0 spade, 1 club, 2 diamond, 3 heart."""
    r = s / 2
    if suit == 2:
        draw.polygon([(cx, cy - r), (cx + r * 0.72, cy), (cx, cy + r), (cx - r * 0.72, cy)], fill=colour)
    elif suit == 3:
        lobe = r * 0.52
        draw.ellipse([cx - 2 * lobe + 0.5, cy - r, cx + 0.5, cy - r + 2 * lobe], fill=colour)
        draw.ellipse([cx - 0.5, cy - r, cx + 2 * lobe - 0.5, cy - r + 2 * lobe], fill=colour)
        draw.polygon([(cx - 2 * lobe + 0.6, cy - r + lobe * 1.15), (cx + 2 * lobe - 0.6, cy - r + lobe * 1.15), (cx, cy + r)], fill=colour)
    elif suit == 0:
        lobe = r * 0.5
        draw.polygon([(cx, cy - r), (cx + 2 * lobe - 0.4, cy + r * 0.15), (cx - 2 * lobe + 0.4, cy + r * 0.15)], fill=colour)
        draw.ellipse([cx - 2 * lobe, cy - lobe * 0.6, cx, cy + lobe * 1.2], fill=colour)
        draw.ellipse([cx, cy - lobe * 0.6, cx + 2 * lobe, cy + lobe * 1.2], fill=colour)
        draw.polygon([(cx, cy + r * 0.2), (cx + r * 0.38, cy + r), (cx - r * 0.38, cy + r)], fill=colour)
    else:
        lobe = r * 0.4
        for dx, dy in ((0, -r + lobe), (-lobe * 1.1, -r * 0.05 + lobe * 0.4), (lobe * 1.1, -r * 0.05 + lobe * 0.4)):
            draw.ellipse([cx + dx - lobe, cy + dy - lobe, cx + dx + lobe, cy + dy + lobe], fill=colour)
        draw.polygon([(cx, cy), (cx + r * 0.36, cy + r), (cx - r * 0.36, cy + r)], fill=colour)


def big(draw_fn, w, h, scale=4):
    """Draws at `scale` times the size and shrinks, for smooth edges."""
    img = Image.new("RGBA", (w * scale, h * scale), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(img), scale)
    return img.resize((w, h), Image.LANCZOS)


# Where the pips go, in 0..1 of the inner field, for 1-10.
PIPS = {
    1: [(0.5, 0.5)],
    2: [(0.5, 0.0), (0.5, 1.0)],
    3: [(0.5, 0.0), (0.5, 0.5), (0.5, 1.0)],
    4: [(0, 0), (1, 0), (0, 1), (1, 1)],
    5: [(0, 0), (1, 0), (0.5, 0.5), (0, 1), (1, 1)],
    6: [(0, 0), (1, 0), (0, 0.5), (1, 0.5), (0, 1), (1, 1)],
    7: [(0, 0), (1, 0), (0.5, 0.25), (0, 0.5), (1, 0.5), (0, 1), (1, 1)],
    8: [(0, 0), (1, 0), (0.5, 0.25), (0, 0.5), (1, 0.5), (0.5, 0.75), (0, 1), (1, 1)],
    9: [(0, 0), (1, 0), (0, 1 / 3), (1, 1 / 3), (0.5, 0.5), (0, 2 / 3), (1, 2 / 3), (0, 1), (1, 1)],
    10: [(0, 0), (1, 0), (0.5, 1 / 6), (0, 1 / 3), (1, 1 / 3), (0, 2 / 3), (1, 2 / 3), (0.5, 5 / 6), (0, 1), (1, 1)],
}
RANKS = {1: "A", 11: "J", 12: "Q", 13: "K"}


def court_emblem(d, k, rank, colour):
    """A crown (king), a coronet (queen) or a helm (jack), 30 x 26 at k times."""
    def p(x, y):
        return (x * k, y * k)
    gold = (196, 160, 72, 255)
    dark = (90, 64, 30, 255)
    if rank == 13:
        d.polygon([p(2, 22), p(2, 8), p(8, 15), p(15, 3), p(22, 15), p(28, 8), p(28, 22)], fill=gold, outline=dark, width=k)
        d.rectangle([p(2, 20), p(28, 25)], fill=dark)
        for x in (8, 15, 22):
            d.ellipse([p(x - 2, 21), p(x + 2, 25)], fill=colour)
    elif rank == 12:
        d.polygon([p(4, 22), p(6, 12), p(11, 16), p(15, 9), p(19, 16), p(24, 12), p(26, 22)], fill=gold, outline=dark, width=k)
        for x, y in ((6, 11), (15, 8), (24, 11)):
            d.ellipse([p(x - 2, y - 2), p(x + 2, y + 2)], fill=colour)
        d.rectangle([p(4, 20), p(26, 24)], fill=dark)
    else:
        d.ellipse([p(5, 4), p(25, 26)], fill=(120, 116, 112, 255), outline=dark, width=k)
        d.rectangle([p(5, 15), p(25, 26)], fill=(120, 116, 112, 255))
        d.rectangle([p(8, 13), p(22, 16)], fill=(30, 26, 26, 255))
        d.rectangle([p(14, 16), p(16, 24)], fill=(30, 26, 26, 255))
        d.polygon([p(15, 0), p(18, 5), p(12, 5)], fill=colour)


def card_face(suit, rank, font):
    img = card_blank(100 + suit * 13 + rank)
    colour = RED if suit >= 2 else INK

    def pips(d, k):
        if rank in PIPS:
            n = PIPS[rank]
            size = 22 if rank == 1 else 11
            for u, v in n:
                cx = (14 + u * 35 + (0 if rank > 3 or True else 0)) if rank > 3 else 31.5
                cy = 20 + v * 58 if rank > 1 else 49
                if rank <= 3:
                    cx = 31.5
                suit_shape(d, suit, cx * k, cy * k, size * k, colour)
        else:
            # The court: an emblem over a big pip, in a thin frame.
            d.rectangle([12 * k, 12 * k, 51 * k, 86 * k], outline=(110, 80, 40, 255), width=k)
            ImageDraw.Draw(Image.new("RGBA", (1, 1)))
            sub = Image.new("RGBA", (30 * k, 26 * k), (0, 0, 0, 0))
            court_emblem(ImageDraw.Draw(sub), k, rank, colour)
            d._image.paste(sub, (17 * k, 18 * k), sub)
            suit_shape(d, suit, 31.5 * k, 62 * k, 26 * k, colour)
        # The corner pip under the rank.
        suit_shape(d, suit, 7 * k, 23 * k, 7 * k, colour)
        suit_shape(d, suit, (CW - 8) * k, (CH - 24) * k, 7 * k, colour)

    img.alpha_composite(big(pips, CW, CH))
    # The rank in the top left corner and, upside down, the bottom right.
    label = RANKS.get(rank, str(rank))
    mark = Image.new("RGBA", (CW, CH), (0, 0, 0, 0))
    magic.lettering(mark, 3 if len(label) == 1 else 1, 3, label, colour, font, scale=1)
    img.alpha_composite(mark)
    img.alpha_composite(mark.rotate(180))
    return img


def joker(font):
    img = card_blank(99)

    def hat(d, k):
        cols = [(150, 26, 22, 255), (40, 60, 120, 255), (196, 160, 72, 255)]
        d.polygon([(31 * k, 52 * k), (8 * k, 26 * k), (20 * k, 50 * k)], fill=cols[0])
        d.polygon([(31 * k, 52 * k), (31 * k, 18 * k), (40 * k, 50 * k)], fill=cols[1])
        d.polygon([(31 * k, 52 * k), (55 * k, 26 * k), (44 * k, 50 * k)], fill=cols[0])
        for x, y in ((8, 26), (31, 18), (55, 26)):
            d.ellipse([(x - 3) * k, (y - 3) * k, (x + 3) * k, (y + 3) * k], fill=cols[2])
        d.rectangle([16 * k, 50 * k, 47 * k, 56 * k], fill=cols[2])
        d.ellipse([22 * k, 56 * k, 41 * k, 76 * k], fill=(222, 206, 170, 255), outline=(60, 40, 30, 255), width=k)
        d.arc([26 * k, 62 * k, 37 * k, 72 * k], 20, 160, fill=(60, 40, 30, 255), width=k)
    img.alpha_composite(big(hat, CW, CH))
    mark = Image.new("RGBA", (CW, CH), (0, 0, 0, 0))
    magic.lettering(mark, 2, 3, "J", RED, font, scale=1)
    img.alpha_composite(mark)
    img.alpha_composite(mark.rotate(180))
    return img


def knot(d, cx, cy, r, k, colour, width):
    """An interlaced bronze ring of three loops."""
    for a in range(3):
        ang = a * 2 * math.pi / 3 - math.pi / 2
        ox, oy = cx + r * 0.45 * math.cos(ang), cy + r * 0.45 * math.sin(ang)
        rr = r * 0.55
        d.ellipse([(ox - rr) * k, (oy - rr) * k, (ox + rr) * k, (oy + rr) * k], outline=colour, width=width * k)
    d.ellipse([(cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k], outline=colour, width=width * k)


def back():
    img = card_blank(98)
    arr = np.asarray(img).astype(float)
    yy, xx = np.mgrid[0:CH, 0:CW].astype(float)
    inner = (xx > 4) & (xx < CW - 5) & (yy > 4) & (yy < CH - 5)
    tone = 0.75 + 0.5 * noise((CH, CW), 6, 97)
    arr[inner, :3] = (ui.MAROON * 2.6)[None, :] * tone[inner, None]
    img = Image.fromarray(arr.astype(np.uint8), "RGBA")

    def pattern(d, k):
        gold = (196, 156, 80, 255)
        d.rectangle([8 * k, 8 * k, (CW - 9) * k, (CH - 9) * k], outline=gold, width=k)
        knot(d, CW / 2, CH / 2, 17, k, gold, 2)
        for cx, cy in ((15, 16), (CW - 16, 16), (15, CH - 17), (CW - 16, CH - 17)):
            knot(d, cx, cy, 5, k, gold, 1)
    img.alpha_composite(big(pattern, CW, CH))
    return img


def shadow_of(img, blur=0):
    a = np.asarray(img)[..., 3]
    out = np.zeros((img.height, img.width, 4), np.uint8)
    out[..., 3] = a
    s = Image.fromarray(out, "RGBA")
    return s.filter(ImageFilter.GaussianBlur(blur)) if blur else s


def dealer():
    """A small hooded card dealer, 32 x 70, his feet at (23, 66)."""
    def fig(d, k):
        robe = (70, 30, 40, 255)
        trim = (190, 150, 70, 255)
        d.polygon([(23 * k, 10 * k), (31 * k, 22 * k), (30 * k, 68 * k), (6 * k, 68 * k), (8 * k, 26 * k), (15 * k, 10 * k)], fill=robe)
        d.line([(19 * k, 22 * k), (18 * k, 68 * k)], fill=trim, width=2 * k)
        d.line([(6 * k, 67 * k), (30 * k, 67 * k)], fill=trim, width=2 * k)
        d.ellipse([12 * k, 2 * k, 28 * k, 22 * k], fill=(56, 22, 32, 255))
        d.ellipse([16 * k, 9 * k, 25 * k, 19 * k], fill=(26, 16, 16, 255))
        d.ellipse([18 * k, 12 * k, 20 * k, 14 * k], fill=(230, 200, 120, 255))
        d.ellipse([22 * k, 12 * k, 24 * k, 14 * k], fill=(230, 200, 120, 255))
        # His hands hold a fan of cards.
        d.ellipse([20 * k, 34 * k, 27 * k, 41 * k], fill=(200, 170, 140, 255))
        for i, a in enumerate((-30, -10, 10)):
            x = 24 + 7 * math.sin(math.radians(a))
            d.polygon([(24 * k, 37 * k), ((x - 3) * k, 28 * k), ((x + 3) * k, 28 * k)], fill=(214, 196, 150, 255), outline=(90, 64, 30, 255))
    img = big(fig, 32, 70)
    arr = np.asarray(img).astype(float)
    yy = np.mgrid[0:70, 0:32][0].astype(float)
    arr[..., :3] *= (1.15 - 0.4 * yy / 70)[..., None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")


def dealer_shadow():
    img = Image.new("RGBA", (40, 33), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([4, 12, 38, 30], fill=(0, 0, 0, 255))
    d.polygon([(10, 22), (2, 2), (14, 6), (26, 22)], fill=(0, 0, 0, 255))
    return img.filter(ImageFilter.GaussianBlur(1.5))


def plate(w, h, text, colour, font, scale, seed):
    """Parchment in a bronze-edged plate with `text` lettered across it."""
    ui.canvas(w, h)
    p = ui.Panel(seed)
    p.colour[:] = PARCHMENT * (0.6 + 0.35 * p.noise(10, 3))[..., None]
    p.height[:] = 0
    rim = ~ui.inside(3, 3, w - 3, h - 3)
    p.colour[rim] = ui.BRONZE * 0.7
    p.height += 2.5 * ui.box(0, 0, w, h, 3)
    img = p.image()
    tw = len(text) * 6 * scale
    magic.lettering(img, (w - tw) // 2, (h - 12 * scale) // 2, text, colour, font, scale=scale)
    ui.canvas(320, 416)
    return img


def table():
    """Dark baize, 576 x 336, with a carved bronze knot in the middle."""
    w, h = 576, 336
    tone = 0.7 + 0.4 * noise((h, w), 40, 7) + 0.12 * (noise((h, w), 3, 8) - 0.5)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    vignette = 1 - 0.45 * (((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2)
    rgb = np.array([34, 52, 38]) * (tone * vignette)[..., None]
    img = Image.fromarray(np.dstack([np.clip(rgb, 0, 255), np.full((h, w), 255)]).astype(np.uint8), "RGBA")

    def emblem(d, k):
        col = (74, 84, 58, 255)
        knot(d, w / 2, h / 2, 70, k, col, 5)
        knot(d, w / 2, h / 2, 30, k, col, 3)
        d.rectangle([24 * k, 24 * k, (w - 25) * k, (h - 25) * k], outline=col, width=3 * k)
    img.alpha_composite(big(emblem, w, h, 2))
    return img


def frame():
    """The carved frame round the table: 640 x 415, open where the table is."""
    ui.canvas(640, 415)
    p = ui.Panel(31)
    for y in (30, 386):
        p.bronze((np.abs(ui.yy - y) < 0.8) & ui.inside(60, 0, 580, 415), 1.5)
        for x in range(90, 560, 64):
            p.boss((x, y), 2.2, 2)
    p.boss((320, 18), 7, 4)
    p.boss((320, 398), 7, 4)
    img = np.asarray(p.image()).copy()
    img[40:376, 32:608, 3] = 0
    # A bronze lip round the opening.
    img[38:40, 30:610, :3] = (ui.BRONZE * 0.8).astype(np.uint8)
    img[376:378, 30:610, :3] = (ui.BRONZE * 0.6).astype(np.uint8)
    img[38:378, 30:32, :3] = (ui.BRONZE * 0.8).astype(np.uint8)
    img[38:378, 608:610, :3] = (ui.BRONZE * 0.6).astype(np.uint8)
    ui.canvas(320, 416)
    return Image.fromarray(img, "RGBA")


def pack(parts, sheet_width):
    """Lays the parts out in rows; returns the sheet and each one's frame."""
    x = y = row = 0
    places = []
    for img in parts:
        if x + img.width > sheet_width:
            x, y, row = 0, y + row + 2, 0
        places.append((x, y))
        x += img.width + 2
        row = max(row, img.height)
    sheet = Image.new("RGBA", (sheet_width, y + row), (0, 0, 0, 0))
    for img, (px, py) in zip(parts, places):
        sheet.paste(img, (px, py))
    return sheet, places


def write(folder, name, parts, anchors, comment, width=1024):
    sheet, places = pack(parts, width)
    folder.mkdir(parents=True, exist_ok=True)
    sheet.save(folder / f"{name}.png", optimize=True)
    with open(folder / f"{name}.sheet", "w", newline="\n") as f:
        f.write(HEADER)
        f.write(f"# {comment}\n")
        f.write(f"image={name}.png\n")
        for img, (x, y), (ox, oy) in zip(parts, places, anchors):
            f.write(f"frame={x},{y},{img.width},{img.height},{ox},{oy}\n")
    shrink.shrink(folder / f"{name}.png")


def card_sheet(out, font):
    parts = [card_face(s, r, font) for s in range(4) for r in range(1, 14)]
    parts += [joker(font), back(), shadow_of(card_blank(98))]
    parts += [dealer(), dealer_shadow()]
    parts += [plate(249, 50, "BLACK JACK", INK, font, 3, 41), plate(249, 50, "BREAK", RED, font, 3, 42)]
    hit, stay = plate(97, 31, "HIT", RED, font, 2, 43), plate(97, 31, "STAY", RED, font, 2, 44)
    winner, loser = plate(117, 38, "WINNER", RED, font, 2, 45), plate(117, 38, "LOSER", RED, font, 2, 46)
    parts += [hit, stay, winner, loser, shadow_of(hit), shadow_of(winner), table()]
    parts += [plate(117, 38, "DRAW", RED, font, 2, 47), plate(97, 31, "HIT", INK, font, 2, 48), plate(97, 31, "STAY", INK, font, 2, 49)]
    parts += [frame()]
    anchors = [(0, 0)] * len(parts)
    anchors[55] = (23, 66)  # the dealer's feet
    anchors[56] = (35, 29)
    write(out / "System/Game/Pattern", "Card", parts, anchors,
          "The card game, numbered as the original's Card.Njp: 52 cards (suit x 13 + rank - 1), joker, back, card shadow, "
          "dealer, his shadow, BLACK JACK, BREAK, HIT, STAY, WINNER, LOSER, button shadow, plate shadow, table, DRAW, "
          "HIT, STAY, then the frame round the table.")


# The Change Color window, in screen pixels (src/game/change_color.cpp).
WX, WY, WW, WH = 160, 144, 320, 192
STAGE = (216, 180, 315, 273)
TABS = [(174, 199), (174, 223), (174, 247)]  # each 41 x 16
SWATCH = (334, 189)  # the first; 32 across, 24 down, 22 x 14


def colour_window(font):
    ui.canvas(WW, WH)
    p = ui.Panel(81)

    def local(x, y):
        return x - WX, y - WY
    x0, y0 = local(*STAGE[:2])
    x1, y1 = local(*STAGE[2:])
    p.sunken(x0, y0, x1, y1, depth=4, floor=ui.MAROON * 1.4)
    sx, sy = local(*SWATCH)
    p.sunken(sx - 6, sy - 6, sx + 3 * 32 + 22 + 6, sy + 3 * 24 + 14 + 6, depth=2, floor=ui.HOLLOW)
    for r in range(4):
        for c in range(4):
            p.sunken(sx + 32 * c - 1, sy + 24 * r - 1, sx + 32 * c + 23, sy + 24 * r + 15, depth=1.5, rim=False, floor=ui.STONE * 0.6)
    for tx, ty in TABS:
        lx, ly = local(tx, ty)
        p.sunken(lx, ly, lx + 41, ly + 16, depth=1.5, floor=ui.STONE * 0.8)
    rx, ry = local(400, 280)
    p.sunken(rx, ry, rx + 64, ry + 16, depth=1.5, floor=ui.STONE * 0.8)
    img = p.image()
    small = Image.new("RGBA", (WW, WH), (0, 0, 0, 0))
    for (tx, ty), name in zip(TABS, ("Weapon", "Shield", "Armor")):
        lx, ly = local(tx, ty)
        magic.lettering(small, lx + (41 - 6 * len(name)) // 2, ly + 2, name, (200, 180, 130, 255), font, scale=1)
    magic.lettering(small, local(400, 280)[0] + 11, local(400, 280)[1] + 2, "Restore", (200, 180, 130, 255), font, scale=1)
    magic.lettering(small, sx + 2, sy - 22, "Color Change Palette", (220, 196, 132, 255), font, scale=1)
    img.alpha_composite(small)
    magic.lettering(img, local(200, 0)[0] + 6, local(0, 300)[1] - 2, "OK", (200, 176, 112, 255), font, scale=2)
    magic.lettering(img, local(336, 0)[0] + 24, local(0, 300)[1] - 2, "CANCEL", (200, 176, 112, 255), font, scale=2)
    ui.canvas(320, 416)
    return img


def lit_text(w, h, text, font, scale, colour):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    glow = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    magic.lettering(glow, (w - len(text) * 6 * scale) // 2, (h - 12 * scale) // 2, text, (255, 220, 140, 255), font, scale=scale)
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(2)))
    magic.lettering(img, (w - len(text) * 6 * scale) // 2, (h - 12 * scale) // 2, text, colour, font, scale=scale)
    return img


def tab(name, font, chosen):
    img = Image.new("RGBA", (41, 16), (0, 0, 0, 0))
    a = np.zeros((16, 41, 4), np.uint8)
    a[..., :3] = (ui.BRONZE * (0.8 if chosen else 0.4)).astype(np.uint8)
    a[..., 3] = 255
    a[1:-1, 1:-1, :3] = (ui.MAROON * 3.2).astype(np.uint8)
    img = Image.fromarray(a, "RGBA")
    magic.lettering(img, (41 - 6 * len(name)) // 2, 2, name, (255, 236, 180, 255), font, scale=1)
    return img


def swatch():
    """22 x 14, white and lit from the top left, for the engine to tint."""
    yy, xx = np.mgrid[0:14, 0:22].astype(float)
    shade = np.clip(1.0 - 0.012 * xx - 0.02 * yy, 0.55, 1)
    edge = np.minimum.reduce([xx, 21 - xx, yy, 13 - yy]) < 1
    shade[edge] *= 0.7
    a = np.dstack([shade * 255] * 3 + [np.full((14, 22), 255)]).astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def colour_sheet(out, font):
    restore = Image.new("RGBA", (64, 16), (0, 0, 0, 0))
    r = np.zeros((16, 64, 4), np.uint8)
    r[..., :3] = (ui.STONE * 0.5).astype(np.uint8)
    r[..., 3] = 255
    restore = Image.fromarray(r, "RGBA")
    magic.lettering(restore, 11, 2, "Restore", (230, 210, 150, 255), font, scale=1)
    parts = [colour_window(font), restore, lit_text(40, 24, "OK", font, 2, (255, 230, 160, 255)),
             lit_text(120, 24, "CANCEL", font, 2, (255, 230, 160, 255))]
    parts += [tab(n, font, True) for n in ("Weapon", "Shield", "Armor")]
    parts += [swatch()]
    anchors = [(-WX, -WY), (-400, -280), (-200, -296), (-336, -296)]
    anchors += [(-x, -y) for x, y in TABS] + [(-SWATCH[0], -SWATCH[1])]
    write(out / "System/Game/Pattern", "ChangeColor", parts, anchors,
          "The Change Color window: window, Restore pushed, OK lit, CANCEL lit, Weapon, Shield and Armor chosen, swatch.", 512)


def main():
    out = Path(sys.argv[1])
    font = Image.open(out / "System/Common/Pattern/Font00.png").convert("RGBA")
    card_sheet(out, font)
    colour_sheet(out, font)
    print("wrote the card game and the Change Color window")


if __name__ == "__main__":
    main()
