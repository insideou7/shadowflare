"""Character animation charts (.caf), from RKC_RPGSCRN_CHARANIM::ReadCafFile.

chart (an action) -> 9 directions -> layers -> frames (pattern, trans, status, priority)."""
import struct

def parse(d):
    assert d[:12] == b"CHRAnimation"
    ver = int(d[12:15]); o = 16
    def rd(fmt):
        nonlocal o
        v = struct.unpack_from(fmt, d, o); o += struct.calcsize(fmt); return v[0]
    charts = []
    for _ in range(rd("<I")):
        status = rd("<h"); dirs = []
        for _dir in range(9):
            nlayers = rd("<I"); maxframes = rd("<h"); layers = []
            for _l in range(nlayers):
                frames = []
                for _f in range(rd("<I")):
                    st = rd("<h"); tr = rd("<h"); pat = rd("<h") if ver <= 1 else rd("<i"); pr = rd("<h")
                    frames.append(dict(status=st, trans=tr, pattern=pat, priority=pr))
                layers.append(frames)
            dirs.append(dict(max_frames=maxframes, layers=layers))
        charts.append(dict(status=status, dirs=dirs))
    extra = None
    if ver != 0 and o + 8 <= len(d): extra = (rd("<i"), rd("<i"))
    return dict(version=ver, charts=charts, extra=extra, end=o, size=len(d))
