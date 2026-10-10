"""Render one sprite pattern to PNG. Also provides write_png and render_pattern helpers.

usage: render.py bank.njp <pattern> out.png [palette] [noflip]
"""
import os
import struct, zlib, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import upd

def write_png(path, w, h, rgba):
    raw = b"".join(b"\0" + bytes(rgba[y*w*4:(y+1)*w*4]) for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))

def part_indices(part, flip=True):
    w, h, bpp, pix = part["w"], part["h"], part["bpp"], part["pixels"]; s = upd.stride(bpp, w)
    rows = []
    for y in range(h):
        row = pix[y*s:(y+1)*s]
        if bpp == 8: idx = list(row[:w])
        elif bpp == 4: idx = [(row[x//2] >> (4 if x % 2 == 0 else 0)) & 15 for x in range(w)]
        else: idx = [(row[x//8] >> (7 - x % 8)) & 1 for x in range(w)]
        rows.append(idx)
    if flip: rows.reverse()
    return rows

def render_pattern(u, pi, palette=None, flip=True, transparent0=True):
    pat = u["patterns"][pi]; x0, y0, w, h = pat["rect"]
    if w <= 0 or h <= 0:
        es = [e for e in pat["parts"] if 0 <= e["part"] < len(u["parts"])]
        if not es: return None
        x0 = min(e["x"] for e in es); y0 = min(e["y"] for e in es)
        w = max(e["x"] + u["parts"][e["part"]]["w"] for e in es) - x0
        h = max(e["y"] + u["parts"][e["part"]]["h"] for e in es) - y0
    pal = u["palettes"][palette if palette is not None else pat["palette"]] if u["palettes"] else [(i*16 % 256,)*3 for i in range(256)]
    img = bytearray(w * h * 4)
    for e in pat["parts"]:
        if not (0 <= e["part"] < len(u["parts"])): continue
        p = u["parts"][e["part"]]
        for yy, row in enumerate(part_indices(p, flip)):
            for xx, c in enumerate(row):
                if transparent0 and c == 0: continue
                X, Y = e["x"] - x0 + xx, e["y"] - y0 + yy
                if 0 <= X < w and 0 <= Y < h:
                    r, g, b = pal[c]; img[(Y*w+X)*4:(Y*w+X)*4+4] = bytes((r, g, b, 255))
    return w, h, img

if __name__ == "__main__":
    u = upd.parse(open(sys.argv[1], "rb").read())
    pi = int(sys.argv[2]); pal = int(sys.argv[4]) if len(sys.argv) > 4 else None
    flip = (sys.argv[5] != "noflip") if len(sys.argv) > 5 else True
    w, h, img = render_pattern(u, pi, pal, flip, transparent0=False if pi == 0 else True)
    write_png(sys.argv[3], w, h, img)
