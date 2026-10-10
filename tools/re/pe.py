"""Minimal PE reader: sections, exports and imports, for annotating disassembly."""
import struct, sys
def load(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3c)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]; optsz = struct.unpack_from("<H", d, pe + 20)[0]
    opt = pe + 24; base = struct.unpack_from("<I", d, opt + 28)[0]
    secs = []
    for i in range(nsec):
        s = pe + 24 + optsz + 40 * i
        name = d[s:s+8].rstrip(b"\0").decode(); vsz, va, rsz, rptr = struct.unpack_from("<IIII", d, s + 8)
        secs.append((name, va, vsz, rptr, rsz))
    def rva2off(r):
        for n, va, vsz, rp, rs in secs:
            if va <= r < va + max(vsz, rs): return r - va + rp
    exp_rva = struct.unpack_from("<I", d, opt + 96)[0]
    exports = {}
    if exp_rva:
        e = rva2off(exp_rva)
        obase, nfun, nnames, af, an, ao = struct.unpack_from("<IIIIII", d, e + 16)
        for i in range(nnames):
            nr = struct.unpack_from("<I", d, rva2off(an) + 4 * i)[0]
            o = struct.unpack_from("<H", d, rva2off(ao) + 2 * i)[0]
            fr = struct.unpack_from("<I", d, rva2off(af) + 4 * o)[0]
            n = d[rva2off(nr):d.index(b"\0", rva2off(nr))].decode()
            exports[n] = base + fr
    return d, base, secs, rva2off, exports
if __name__ == "__main__":
    d, base, secs, r2o, ex = load(sys.argv[1])
    for n, a in sorted(ex.items(), key=lambda kv: kv[1]): print(hex(a), n)

def imports(path):
    d, base, secs, r2o, _ = load(path)
    pe = struct.unpack_from("<I", d, 0x3c)[0]; opt = pe + 24
    imp_rva = struct.unpack_from("<I", d, opt + 104)[0]
    res = {}; o = r2o(imp_rva)
    while True:
        ilt, _, _, name, iat = struct.unpack_from("<IIIII", d, o)
        if not name: break
        dll = d[r2o(name):d.index(b"\0", r2o(name))].decode()
        t = r2o(ilt or iat); k = 0
        while True:
            e = struct.unpack_from("<I", d, t + 4 * k)[0]
            if not e: break
            if e & 0x80000000: n = f"{dll}#{e & 0xffff}"
            else: n = d[r2o(e) + 2:d.index(b"\0", r2o(e) + 2)].decode()
            res[base + iat + 4 * k] = n; k += 1
        o += 20
    return res

def annotate(path, asm_path):
    import re
    imp = imports(path); _, _, _, _, ex = load(path); rev = {v: k for k, v in ex.items()}
    out = []
    for line in open(asm_path):
        m = re.search(r"ds:0x([0-9a-f]+)", line)
        if m and int(m.group(1), 16) in imp: line = line.rstrip() + "   ; " + imp[int(m.group(1), 16)] + "\n"
        m = re.search(r"call\s+0x([0-9a-f]+)", line)
        if m and int(m.group(1), 16) in rev: line = line.rstrip() + "   ; " + rev[int(m.group(1), 16)] + "\n"
        out.append(line)
    return "".join(out)
