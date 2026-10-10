"""Ground layer (.gnd), from RKC_RPGSCRN_GROUNDBLOCK::ReadFile."""
import os
import struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); from rclib import unpack_at

def _block(d, o, size):
    flag = d[o]; o += 1
    if flag:
        data, usize, csize, _ = unpack_at(d, o); return data, o + 16 + csize
    return d[o:o+size], o + size

def parse(d):
    assert d[:15] == b"RPGSCRN_GNDv000"
    w, h, cw, ch, magx, magy = struct.unpack_from("<6i", d, 16)
    area, o = _block(d, 40, w * h * 6)
    n = w * h
    planes = [struct.unpack_from(f"<{n}h", area, k * n * 2) for k in range(3)]
    rest = d[o:]
    return dict(w=w, h=h, chip_w=cw, chip_h=ch, mag=(magx, magy), planes=planes, rest_offset=o, size=len(d))

if __name__ == "__main__":
    import collections
    g = parse(open(sys.argv[1], "rb").read())
    print({k: v for k, v in g.items() if k != "planes"})
    for i, p in enumerate(g["planes"]):
        c = collections.Counter(p); print("plane", i, "distinct", len(c), "min", min(p), "max", max(p), c.most_common(6))
