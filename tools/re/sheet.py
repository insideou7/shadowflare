"""Contact sheet of sprite patterns.

usage: sheet.py bank.njp out.png <first> <count> [palette|-] [columns]
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import upd, render
# usage: sheet.py file out.png first count [palette] [cols]
u = upd.parse(open(sys.argv[1], "rb").read())
first, count = int(sys.argv[3]), int(sys.argv[4]); pal = int(sys.argv[5]) if len(sys.argv) > 5 and sys.argv[5] != "-" else None
cols = int(sys.argv[6]) if len(sys.argv) > 6 else 8
imgs = [render.render_pattern(u, i, pal) for i in range(first, min(first + count, len(u["patterns"])))]
imgs = [i for i in imgs if i]
cw = max(i[0] for i in imgs) + 4; ch = max(i[1] for i in imgs) + 4; rows = (len(imgs) + cols - 1) // cols
W, H = cw * cols, ch * rows; out = bytearray(bytes((40, 40, 48, 255)) * W * H)
for k, (w, h, img) in enumerate(imgs):
    ox, oy = (k % cols) * cw + 2, (k // cols) * ch + 2
    for y in range(h):
        for x in range(w):
            px = img[(y*w+x)*4:(y*w+x)*4+4]
            if px[3]: out[((oy+y)*W+ox+x)*4:((oy+y)*W+ox+x)*4+4] = px
render.write_png(sys.argv[2], W, H, out)
print(len(imgs), "patterns", W, "x", H)
