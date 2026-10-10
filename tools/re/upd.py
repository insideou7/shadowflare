"""Parser for UPD sprite banks: .njp (NJudgeUniPat/UnitePatData) and .sdw (ShadowLowPat).
Layout reverse engineered from RKC_UPDIB_UPD::Read in RKC_UPDIB.dll."""
import struct
from rclib import unpack_at, MAGIC

class Reader:
    def __init__(s, d, o=0): s.d, s.o = d, o
    def u32(s): v = struct.unpack_from("<I", s.d, s.o)[0]; s.o += 4; return v
    def i32(s): v = struct.unpack_from("<i", s.d, s.o)[0]; s.o += 4; return v
    def raw(s, n): v = s.d[s.o:s.o+n]; s.o += n; return v

def stride(bpp, w):
    if bpp == 1: return (((w + 7) // 8) + 3) // 4 * 4
    if bpp == 4: return (((w + 1) // 2) + 3) // 4 * 4
    return (w + 3) // 4 * 4

def parse(d):
    r = Reader(d); magic = r.raw(16)
    kind = magic[:12].decode()
    version = int(magic[12:15])
    shadow = kind == "ShadowLowPat"; judge = kind == "UnitePatData"
    nparts = r.u32()
    if version >= 3: r.u32()
    parts = []
    for _ in range(nparts):
        bpp, w, h, comp = r.u32(), r.u32(), r.u32(), r.u32()
        if shadow: bpp = 1
        size = stride(bpp, w) * h
        if comp:
            pix, usize, csize, _ = unpack_at(d, r.o); r.o += 16 + csize
        else:
            pix = r.raw(size) if size else b""
        parts.append(dict(bpp=bpp, w=w, h=h, pixels=pix))
    npat = r.u32()
    if version >= 3: r.u32()
    patterns = []
    for _ in range(npat):
        n = r.u32(); rect = struct.unpack("<4i", r.raw(16))
        j = r.raw(0xa8) if judge else None
        pal = r.i32() if version >= 1 else 0
        lst = []
        for _ in range(n):
            f0 = r.i32(); part = r.i32(); x, y = struct.unpack("<2i", r.raw(8)); a, b, c = r.i32(), r.i32(), r.i32()
            lst.append(dict(f0=f0, part=part, x=x, y=y, a=a, b=b, c=c))
        patterns.append(dict(rect=rect, palette=pal, parts=lst, judge=j))
    npal = r.u32(); palettes = []
    for _ in range(npal):
        raw = r.raw(1024); palettes.append([(raw[i], raw[i+1], raw[i+2]) for i in range(0, 1024, 4)])
    names = []
    if version >= 2 and r.o + 4 <= len(d):
        for _ in range(npat):
            if r.o + 4 > len(d): break
            n = r.u32(); names.append(r.raw(n).decode("cp932", "replace"))
    return dict(kind=kind, version=version, parts=parts, patterns=patterns, palettes=palettes, names=names, end=r.o, size=len(d))
