#!/usr/bin/env python3
"""The Create New Character screen for the open content pack.

    python3 tools/art/select_screen.py <portraits folder> <content pack folder>

Draws System/Select/Pattern/Select: a picture for each of the original's
Select.njp patterns that the engine uses, numbered as the original numbers
them, each anchored at its place on the 640 x 480 screen (see
src/game/new_character.cpp, which takes its click areas from
ShadowFlare.exe):

- 41, the background: dark stone in an iron frame with bronze trim;
- 36, the title; the words of the buttons, dim and lit (0-5, 20-25, 34-35);
- 7-10, the hero and the heroine, dim and lit, from portraits.py; 11 and
  12, their shades, drawn behind them at half strength;
- 38, the window that asks for the game mode.

Our own art, drawn from scratch in the original's manner: engraved bronze
capitals on stone. The words are set in Liberation Serif Bold (SIL Open
Font Licence). Patterns the engine does not use are left empty. Needs
numpy and Pillow, then shrink.py makes the picture 256 colours.
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import shrink  # noqa: E402
from ui import write_sheet  # noqa: E402

FONTS = ["/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
         "/usr/share/fonts/liberation/LiberationSerif-Bold.ttf",
         "/usr/share/fonts/truetype/liberation2/LiberationSerif-Bold.ttf"]

STONE = np.array([40, 42, 48])
IRON = np.array([52, 50, 50])
BRONZE = np.array([168, 130, 70])
GOLD_DIM = ((120, 98, 62), (70, 54, 30))
GOLD_LIT = ((255, 236, 170), (196, 150, 64))

# Pattern: (screen x, y, width, height) of the box the words fit in, and the words.
WORDS = {
    0: ((575, 7, 59, 14), "EXIT"), 2: ((392, 450, 75, 14), "BACK"), 4: ((566, 450, 39, 14), "OK"),
    6: ((61, 450, 109, 14), "DELETE"), 14: ((247, 275, 51, 14), "YES"), 16: ((348, 275, 40, 14), "NO"),
    18: ((368, 275, 39, 14), "OK"), 20: ((283, 278, 75, 14), "BACK"), 22: ((219, 207, 203, 14), "ONLINE MODE"),
    24: ((221, 241, 198, 14), "SINGLE MODE"), 26: ((240, 207, 160, 14), "NEW GAME"),
    28: ((232, 241, 168, 14), "JOIN GAME"), 34: ((96, 410, 83, 14), "NAME"),
}
LIT = {0: 1, 2: 3, 4: 5, 6: 13, 14: 15, 16: 17, 18: 19, 20: 21, 22: 23, 24: 25, 26: 27, 28: 29, 34: 35}
TITLE = ((48, 16, 272, 40), "CREATE NEW CHARACTER")
# The figures: where the bottom middle of each stands, as the original's.
FIGURES = {"hero": ((228, 391), 7, 8, 11), "heroine": ((453, 390), 9, 10, 12)}
FIGURE_HEIGHT = 300  # as tall as the original's figures
MODE_WINDOW = (176, 154, 288, 171)
COUNT = 42


def font(size):
    for path in FONTS:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    raise SystemExit("select_screen.py needs Liberation Serif Bold (fonts-liberation)")


def noise(w, h, scale, rng, octaves=4):
    total, amp, norm = np.zeros((h, w)), 1.0, 0.0
    for o in range(octaves):
        step = max(1, int(scale / 2 ** o))
        grid = rng.random((h // step + 2, w // step + 2))
        big = Image.fromarray((grid * 255).astype(np.uint8)).resize(((w // step + 2) * step, (h // step + 2) * step),
                                                                     Image.BICUBIC)
        total += amp * np.asarray(big, dtype=float)[:h, :w] / 255
        norm += amp
        amp *= 0.5
    return total / norm


def shade(colour, height):
    """Colours lit from the top left by a height map."""
    gy, gx = np.gradient(height)
    light = np.clip(1.0 - 0.2 * gx - 0.2 * gy, 0.35, 1.6)
    return np.clip(colour * light[..., None], 0, 255).astype(np.uint8)


def framed_stone(w, h, seed, floor=STONE, border=10):
    """Blotchy stone in an iron frame with a bronze line inside it."""
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w].astype(float)
    blotch, grain, vein = noise(w, h, 64, rng), noise(w, h, 6, rng, 2), noise(w, h, 32, rng, 3)
    cracks = np.clip(1 - np.abs(vein - 0.5) * 40, 0, 1) * 0.3
    colour = floor * (0.75 + 0.45 * blotch - cracks + 0.12 * (grain - 0.5))[..., None]
    height = 0.6 * grain + 0.8 * blotch - 2 * cracks
    edge = np.minimum.reduce([xx, w - 1 - xx, yy, h - 1 - yy])
    frame = edge < border
    colour[frame] = (IRON * (0.75 + 0.5 * noise(w, h, 10, rng, 3)[..., None]))[frame]
    height += np.where(frame, 5 * np.clip(edge / 3, 0, 1), 0)
    trim = (edge >= border) & (edge < border + 2)
    colour[trim] = BRONZE * 0.9
    height[trim] += 2.5
    trim2 = (edge >= 3) & (edge < 4.5)
    colour[trim2] = BRONZE * 0.7
    height[trim2] += 1.5
    # Studs along the frame.
    studs = [(x, border / 2) for x in range(30, w - 20, 40)] + [(x, h - border / 2) for x in range(30, w - 20, 40)]
    studs += [(border / 2, y) for y in range(30, h - 20, 40)] + [(w - border / 2, y) for y in range(30, h - 20, 40)]
    for cx, cy in studs:
        bump = np.clip(1 - ((xx - cx) ** 2 + (yy - cy) ** 2) / 2.2 ** 2, 0, 1)
        colour = np.where((bump > 0)[..., None], BRONZE * (0.7 + 0.3 * bump)[..., None], colour)
        height += 4 * np.sqrt(bump)
    return np.concatenate([shade(colour, height), np.full((h, w, 1), 255, np.uint8)], axis=2)


def engraved(text, box, colours, size=None):
    """Bronze capitals with a dark edge, fitted into a box (w, h)."""
    w, h = box
    big = 4
    f = font(size or h * big)
    left, top, right, bottom = f.getbbox(text)
    tw, th = right - left, bottom - top
    mask = Image.new("L", (tw + 8, th + 8), 0)
    ImageDraw.Draw(mask).text((4 - left, 4 - top), text, font=f, fill=255)
    # Fit the box: shrink to its height, and narrow it if still too wide.
    scale = (h - 2) / th
    nw, nh = max(1, round(tw * scale)), h - 2
    if nw > w - 2:
        nw = w - 2
    mask = mask.resize((nw + 2, nh + 2), Image.LANCZOS)
    m = np.asarray(mask, dtype=float) / 255
    # Top to bottom from light to dark, as polished bronze catches the light.
    top_c, bottom_c = np.array(colours[0], float), np.array(colours[1], float)
    t = np.linspace(0, 1, m.shape[0])[:, None, None]
    fill = top_c * (1 - t) + bottom_c * t
    fill = np.broadcast_to(fill, (m.shape[0], m.shape[1], 3))
    edge = np.asarray(mask.filter(ImageFilter.MaxFilter(3)), dtype=float) / 255
    out = np.zeros((h, w, 4))
    x0, y0 = (w - m.shape[1]) // 2, (h - m.shape[0]) // 2
    region = (slice(max(0, y0), max(0, y0) + m.shape[0]), slice(max(0, x0), max(0, x0) + m.shape[1]))
    sub_h, sub_w = out[region].shape[:2]
    rgb = fill[:sub_h, :sub_w] * m[:sub_h, :sub_w, None]
    alpha = np.maximum(m[:sub_h, :sub_w], edge[:sub_h, :sub_w] * 0.85)
    out[region][..., :3] = rgb / np.maximum(alpha[..., None], 1e-6)
    out[region][..., 3] = alpha * 255
    return Image.fromarray(np.clip(out, 0, 255).astype(np.uint8), "RGBA")


def main():
    portraits, out = Path(sys.argv[1]), Path(sys.argv[2])
    tiles = [None] * COUNT  # (image, screen x, screen y)
    tiles[41] = (Image.fromarray(framed_stone(640, 480, 11)), 0, 0)
    for pattern, ((x, y, w, h), text) in WORDS.items():
        tiles[pattern] = (engraved(text, (w, h), GOLD_DIM), x, y)
        tiles[LIT[pattern]] = (engraved(text, (w, h), GOLD_LIT), x, y)
    (x, y, w, h), text = TITLE
    tiles[36] = (engraved(text, (w, h), ((236, 214, 160), (130, 100, 52))), x, y)
    wx, wy, ww, wh = MODE_WINDOW
    window = framed_stone(ww, wh, 12, floor=np.array([52, 22, 22]), border=8)
    tiles[38] = (Image.fromarray(window), wx, wy)
    for name, ((bx, by), dim, lit, shade_pattern) in FIGURES.items():
        im = Image.open(portraits / f"{name}.png").convert("RGBA")
        im = im.crop(im.getbbox())
        scale = FIGURE_HEIGHT / im.height
        im = im.resize((round(im.width * scale), FIGURE_HEIGHT), Image.LANCZOS)
        x, y = bx - im.width // 2, by - im.height
        tiles[lit] = (im, x, y)
        a = np.asarray(im, dtype=float)
        darker = a.copy()
        darker[..., :3] *= 0.62
        tiles[dim] = (Image.fromarray(darker.astype(np.uint8), "RGBA"), x, y)
        # The shade: the figure's outline in black, cast back and to the right.
        silhouette = np.zeros_like(a)
        silhouette[..., 3] = a[..., 3]
        tiles[shade_pattern] = (Image.fromarray(silhouette.astype(np.uint8), "RGBA"), x + 14, y + 6)

    # One sheet, the tiles in rows.
    blank = (Image.new("RGBA", (1, 1)), 0, 0)
    tiles = [t or blank for t in tiles]
    width = 1024
    cx = cy = row = 0
    places = []
    for im, x, y in tiles:
        if cx + im.width > width:
            cx, cy, row = 0, cy + row, 0
        places.append((cx, cy))
        cx += im.width
        row = max(row, im.height)
    sheet = Image.new("RGBA", (width, cy + row))
    frames = []
    for (im, x, y), (px, py) in zip(tiles, places):
        sheet.alpha_composite(im, (px, py))
        frames.append((px, py, im.width, im.height, -x, -y))
    write_sheet(out / "System/Select/Pattern", "Select", sheet, frames,
                "The Create New Character screen, by the original's Select.njp pattern numbers; "
                "each anchor puts its picture at its place on the screen.")
    sheet_file = out / "System/Select/Pattern/Select.sheet"
    sheet_file.write_text(sheet_file.read_text().replace("tools/art/ui.py", "tools/art/select_screen.py"))
    shrink.shrink(out / "System/Select/Pattern/Select.png")
    print("wrote the Create New Character screen")


if __name__ == "__main__":
    main()
