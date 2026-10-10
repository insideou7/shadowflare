"""Object layer (.obl), from RKC_RPGSCRN_OBJECTBLOCK::ReadFile."""
import struct
def parse(d):
    assert d[:12] == b"RPGSCRN_OBJv"
    ver = int(d[12:15]); n = struct.unpack_from("<i", d, 16)[0]; o = 20; objs = []
    for _ in range(n):
        x, y = struct.unpack_from("<2i", d, o); o += 8
        f = struct.unpack_from("<6h", d, o); o += 12
        g = struct.unpack_from("<3h", d, o) if ver >= 1 else (1000, 1000, 1000); o += 6 if ver >= 1 else 0
        rect = struct.unpack_from("<4i", d, o); o += 16
        objs.append(dict(x=x, y=y, file=f[0], pattern=f[1], status=f[2], trans=f[3], height=f[4], palette=f[5], rgb=g, judge=rect))
    return dict(version=ver, objects=objs, end=o, size=len(d))
