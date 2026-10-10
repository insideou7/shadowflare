#!/usr/bin/env python3
"""Survey a folder of original game files.

Prints every file with its size and a guess at its type from the first
bytes, then a summary by extension. Run it on your own copy of the game:

    python3 tools/inventory.py Shadowflare > inventory.txt
"""
import collections
import pathlib
import struct
import sys

MAGIC = [
    (b"MZ", "DOS/Windows executable"),
    (b"RIFF", "RIFF (WAV/AVI)"),
    (b"BM", "Windows bitmap"),
    (b"GIF8", "GIF image"),
    (b"\x89PNG", "PNG image"),
    (b"\xff\xd8\xff", "JPEG image"),
    (b"OggS", "Ogg audio"),
    (b"ID3", "MP3 audio"),
    (b"MThd", "MIDI music"),
    (b"PK\x03\x04", "ZIP archive"),
    (b"MPQ\x1a", "MPQ archive"),
    (b"\x00\x00\x01\xba", "MPEG video"),
    (b"Smacker", "Smacker video"),
    (b"SMK2", "Smacker video"),
    (b"BIK", "Bink video"),
]


def describe(head: bytes, path: pathlib.Path) -> str:
    for magic, name in MAGIC:
        if head.startswith(magic):
            if magic == b"MZ":
                return describe_exe(path, name)
            if magic == b"RIFF" and len(head) >= 12:
                return f"RIFF {head[8:12].decode('latin-1')}"
            return name
    if head and all(32 <= b < 127 or b in (9, 10, 13) for b in head[:64]):
        return "text?"
    return "unknown: " + head[:8].hex(" ")


def describe_exe(path: pathlib.Path, fallback: str) -> str:
    try:
        data = path.read_bytes()[:4096]
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\x00\x00":
            return fallback
        machine, sections = struct.unpack_from("<HH", data, pe + 4)
        chars = struct.unpack_from("<H", data, pe + 22)[0]
        kind = "DLL" if chars & 0x2000 else "EXE"
        arch = {0x14C: "x86", 0x8664: "x64"}.get(machine, hex(machine))
        return f"PE {kind} {arch}, {sections} sections"
    except (struct.error, OSError):
        return fallback


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "Shadowflare")
    if not root.is_dir():
        print(f"not a folder: {root}", file=sys.stderr)
        return 1
    by_ext = collections.defaultdict(lambda: [0, 0])
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        size = path.stat().st_size
        with path.open("rb") as f:
            head = f.read(64)
        ext = path.suffix.lower() or "(none)"
        by_ext[ext][0] += 1
        by_ext[ext][1] += size
        print(f"{size:>12,}  {path.relative_to(root)}  [{describe(head, path)}]")
    print("\nBy extension:")
    for ext, (count, total) in sorted(by_ext.items(), key=lambda kv: -kv[1][1]):
        print(f"  {ext:<10} {count:>6} files {total:>14,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
