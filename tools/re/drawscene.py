"""Render a whole map (ground + objects) to PNG.

usage: drawscene.py <install>/Map <map name> out.png <offset x> <offset y> [downscale]
"""
import os
import sys, pathlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import upd, gnd, obl, render
mapdir = pathlib.Path(sys.argv[1]); name = sys.argv[2]; out = sys.argv[3]
offx, offy = int(sys.argv[4]), int(sys.argv[5]); scale = int(sys.argv[6]) if len(sys.argv) > 6 else 2
def find(dirp, fname):
    for p in dirp.iterdir():
        if p.name.lower() == fname.lower(): return p
lst = [l.strip() for l in open(find(mapdir / "Pattern", name + ".Lst"), encoding="latin-1").read().splitlines()]
g = gnd.parse(find(mapdir / "Ground", name + ".Gnd").read_bytes())
ob = obl.parse(find(mapdir / "Object", name + ".Obl").read_bytes())["objects"]
banks = {}; cache = {}
def bank(fi):
    if fi not in banks:
        banks[fi] = None
        if 0 <= fi < len(lst) and lst[fi] != "?":
            p = find(mapdir / "Pattern", lst[fi]); banks[fi] = upd.parse(p.read_bytes()) if p else None
    return banks[fi]
def pat(fi, pi, opaque):
    k = (fi, pi)
    if k not in cache:
        u = bank(fi); cache[k] = None
        if u and 0 <= pi < len(u["patterns"]):
            r = render.render_pattern(u, pi, transparent0=not opaque)
            if r:
                p = u["patterns"][pi]; x0, y0 = p["rect"][0], p["rect"][1]
                if p["rect"][2] <= 0:
                    es = [e for e in p["parts"] if 0 <= e["part"] < len(u["parts"])]
                    x0 = min(e["x"] for e in es); y0 = min(e["y"] for e in es)
                cache[k] = (x0, y0) + r
    return cache[k]
cw, ch = g["chip_w"], g["chip_h"]; W, H = g["w"] * cw, g["h"] * ch
img = bytearray(W * H * 4)
def blit(X, Y, t, alpha):
    x0, y0, tw, th, px = t
    for y in range(th):
        YY = Y + y0 + y
        if not 0 <= YY < H: continue
        for x in range(tw):
            p = px[(y*tw+x)*4:(y*tw+x)*4+4]
            if alpha and not p[3]: continue
            XX = X + x0 + x
            if 0 <= XX < W: img[(YY*W+XX)*4:(YY*W+XX)*4+4] = p
for cy in range(g["h"]):
    for cx in range(g["w"]):
        k = cy * g["w"] + cx; t = pat(g["planes"][1][k], g["planes"][2][k], True)
        if t: blit(cx * cw - t[0], cy * ch - t[1], t, False)
items = []
for o in ob:
    sx = (o["x"] - o["y"]) * 15 // 100 + offx; sy = (o["x"] + o["y"]) * 10 // 100 + offy
    items.append((sy, sx, o))
for sy, sx, o in sorted(items, key=lambda t: t[0]):
    if lst[o["file"]].lower().endswith(".sdw") if 0 <= o["file"] < len(lst) else True: continue
    t = pat(o["file"], o["pattern"], False)
    if t: blit(sx, sy, t, True)
if scale > 1:
    W2, H2 = W // scale, H // scale; small = bytearray(W2 * H2 * 4)
    for y in range(H2):
        row = img[(y*scale)*W*4:(y*scale+1)*W*4]
        small[y*W2*4:(y+1)*W2*4] = b"".join(row[x*scale*4:x*scale*4+4] for x in range(W2))
    img, W, H = small, W2, H2
render.write_png(out, W, H, img)
print(name, len(ob), "objects", W, "x", H)
