"""Compose one animation frame of a character to PNG.
usage: animframe.py <dir with Animation*.Caf/.Njp/.Sdw> <stem> out.png chart dir frame [layers,comma|all] [cols]"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import caf, upd, render
base, stem, out = sys.argv[1], sys.argv[2], sys.argv[3]
chart, direction = int(sys.argv[4]), int(sys.argv[5])
frames = [int(x) for x in sys.argv[6].split(",")]
sel = sys.argv[7] if len(sys.argv) > 7 else "all"
c = caf.parse(open(f"{base}/{stem}.Caf", "rb").read())
n = upd.parse(open(f"{base}/{stem}.Njp", "rb").read())
def comp(frame):
    d = c["charts"][chart]["dirs"][direction]
    cells = []
    for li, layer in enumerate(d["layers"]):
        if sel != "all" and str(li) not in sel.split(","): continue
        if frame < len(layer) and layer[frame]["status"] != 8: cells.append((layer[frame]["priority"], li, layer[frame]))
    cells.sort()
    W, H, ox, oy = 240, 240, 120, 180
    img = bytearray(W * H * 4)
    for _, li, cell in cells:
        p = cell["pattern"]
        r = render.render_pattern(n, p)
        if not r: continue
        pat = n["patterns"][p]; x0, y0 = pat["rect"][0], pat["rect"][1]
        if pat["rect"][2] <= 0:
            es = [e for e in pat["parts"] if 0 <= e["part"] < len(n["parts"])]
            x0 = min(e["x"] for e in es); y0 = min(e["y"] for e in es)
        w, h, px = r
        for y in range(h):
            for x in range(w):
                q = px[(y*w+x)*4:(y*w+x)*4+4]
                X, Y = ox + x0 + x, oy + y0 + y
                if q[3] and 0 <= X < W and 0 <= Y < H: img[(Y*W+X)*4:(Y*W+X)*4+4] = q
    return W, H, img
ims = [comp(f) for f in frames]
W = ims[0][0] * len(ims); H = ims[0][1]; out_img = bytearray(W * H * 4)
for k, (w, h, im) in enumerate(ims):
    for y in range(h): out_img[(y*W + k*w)*4:(y*W + k*w + w)*4] = im[y*w*4:(y+1)*w*4]
render.write_png(out, W, H, out_img)
