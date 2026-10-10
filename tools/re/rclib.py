import struct, sys
MAGIC = b"RCLIB-L\x1a"
def lzss(src, out_size, N=4096, fill=0):
    # Reverse engineered from RK_LzDecodeMemoryToMemory (RK_FUNCTION.dll).
    # Flag bits are read MSB first; a set bit means a back reference.
    ring = bytearray([fill]) * N; r = 0xFEE; out = bytearray(); i = 0
    while len(out) < out_size and i < len(src):
        flags = src[i]; i += 1; mask = 0x80
        while mask and len(out) < out_size and i < len(src):
            if flags & mask:
                if i + 1 >= len(src): break
                a, b = src[i], src[i+1]; i += 2
                p = a | ((b & 0xf0) << 4); n = (b & 0x0f) + 3
                for k in range(n):
                    c = ring[(p + k) & 0xfff]; out.append(c); ring[r] = c; r = (r + 1) & 0xfff
            else:
                c = src[i]; i += 1; out.append(c); ring[r] = c; r = (r + 1) & 0xfff
            mask >>= 1
    return bytes(out), i
def unpack_at(data, off):
    assert data[off:off+8] == MAGIC
    usize, csize = struct.unpack_from("<II", data, off + 8)
    out, used = lzss(data[off+16:off+16+csize], usize)
    return out, usize, csize, used
if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read(); off = d.find(MAGIC)
    out, u, c, used = unpack_at(d, off)
    print(f"header {off}  unpacked {u} got {len(out)}  packed {c} used {used}  file {len(d)} end {off+16+c}")
    if len(sys.argv) > 2: open(sys.argv[2], "wb").write(out)
