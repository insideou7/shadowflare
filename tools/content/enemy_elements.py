#!/usr/bin/env python3
"""Give the open pack's monsters the elements of the original's. Needs the
player's own copy of the game.

    python3 tools/content/enemy_elements.py <game folder> <content pack>

Each enemy= line of a scenario first_area.py laid out after an original
one (Scenario/1xxxxxxx) takes the element of the monster it stands for,
placement by placement, so it fights the same: an element changes what
the hero's blows and spells do to it and what its own do to the hero (see
docs/magic.md). The demo's scenarios (1 to 7) have no original to follow;
their monsters take the element the original gives that kind most often.
The lines get the two fields an element needs, magic defence (kept at 0,
as before) and the element (0 fire, 1 water, 2 earth, 3 thunder, 4 holy,
5 dark, 6 gel, 7 metal); a line that already has them keeps its magic
defence. Only the element number is taken from the original.
"""

import collections
import sys
from pathlib import Path

from first_area import OFFSET, find, read_mct


def with_element(line, element):
    """An enemy= line with `element`, its comment kept."""
    body, hash_, comment = line.partition("#")
    v = [f.strip() for f in body.strip()[len("enemy="):].split(",")]
    if len(v) == 16:
        v += ["0"]
    elif len(v) != 18:
        raise SystemExit(f"cannot give an element to: {line}")
    v = v[:17] + [str(element)]
    out = "enemy=" + ",".join(v)
    return out + ("  #" + comment if hash_ else "")


def main():
    game, out = Path(sys.argv[1]), Path(sys.argv[2])
    if not (game / "Scenario").is_dir():
        game = next(p.parent for p in game.rglob("Scenario") if (p.parent / "Map").is_dir())
    kinds = {int(p.name) for p in (out / "Character" / "ENEMY").iterdir()}

    originals = {}
    by_kind = collections.defaultdict(collections.Counter)
    for sc in sorted((game / "Scenario").iterdir()):
        mct = find(sc, "Scenario", ".mct")
        if not mct:
            continue
        enemies = read_mct(mct.read_bytes())[2][2]
        originals[int(sc.name)] = enemies
        for c in enemies:
            by_kind[c["kind"]][c["words"][6]] += 1
    usual = {k: counts.most_common(1)[0][0] for k, counts in by_kind.items()}

    for f in sorted((out / "Scenario").glob("*/scenario.txt")):
        number = int(f.parent.name)
        lines = f.read_text().split("\n")
        ours = [i for i, line in enumerate(lines) if line.startswith("enemy=")]
        if not ours:
            continue
        theirs = [c for c in originals.get(number - OFFSET, []) if c["kind"] in kinds] if number >= OFFSET else []
        if number >= OFFSET and len(theirs) != len(ours):
            raise SystemExit(f"{f}: {len(ours)} monsters but the original has {len(theirs)}")
        for n, i in enumerate(ours):
            kind = int(lines[i][len("enemy="):].split(",")[0])
            if theirs:
                if theirs[n]["kind"] != kind:
                    raise SystemExit(f"{f}: monster {n} is kind {kind}, the original's {theirs[n]['kind']}")
                element = theirs[n]["words"][6]
            else:
                element = usual.get(kind, 0)
            lines[i] = with_element(lines[i], element)
        f.write_text("\n".join(lines))
        print(f"{f.parent.name}: {len(ours)} monsters" + (" (as the original's)" if theirs else " (by kind)"))


if __name__ == "__main__":
    main()
