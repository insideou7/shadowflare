"""Seed corpus for fuzz_formats: small files from the open pack (and, when
given, the player's own original files), each prefixed with the byte that
picks its reader in tests/fuzz/fuzz_formats.cpp.

    python3 tests/fuzz/make_corpus.py content [game] build-fuzz/corpus

The corpus is local only: never commit it, since seeds from game/ are the
original's files.
"""
import hashlib
import pathlib
import sys

MAX_SIZE = 64 * 1024
PER_KIND = 12

# Reader numbers as in fuzz_formats.cpp's readWith.
BY_SUFFIX = {
    ".caf": 1, ".gnd": 4, ".obl": 5, ".mct": 6, ".scs": 7, ".njp": 8, ".sdw": 8,
    ".png": 12, ".sheet": 13, ".anim": 14,
}


def reader_for(path: pathlib.Path, root: pathlib.Path):
    suffix = path.suffix.lower()
    if suffix in BY_SUFFIX:
        return BY_SUFFIX[suffix]
    if suffix == ".txt":
        top = path.relative_to(root).parts[0]
        if top == "Map":
            return 15
        if top == "Scenario":
            return 16
        if path.name in ("items.txt", "uniques.txt"):
            return 17
    return None


def main():
    *sources, out = sys.argv[1:]
    out = pathlib.Path(out)
    out.mkdir(parents=True, exist_ok=True)
    counts = {}
    for source in map(pathlib.Path, sources):
        for path in sorted(source.rglob("*")):
            if not path.is_file() or path.stat().st_size > MAX_SIZE:
                continue
            reader = reader_for(path, source)
            if reader is None or counts.get(reader, 0) >= PER_KIND:
                continue
            data = bytes([reader]) + path.read_bytes()
            (out / hashlib.sha1(data).hexdigest()).write_bytes(data)
            counts[reader] = counts.get(reader, 0) + 1
    print(f"{sum(counts.values())} seeds in {out}: {dict(sorted(counts.items()))}")


if __name__ == "__main__":
    main()
