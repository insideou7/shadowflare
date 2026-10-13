#!/usr/bin/env python3
"""Give the open pack's monsters the original's stats. Needs the player's
own copy of the game.

    python3 tools/content/enemy_stats.py <game folder> <content pack>

Each enemy= line of a scenario first_area.py laid out after an original
one (Scenario/1xxxxxxx) takes the numbers of the monster it stands for,
placement by placement, so it fights as the original's does: health,
attacks and their hit, defence, evasion, magic defence and evasion,
element, spells, experience, gold, speed and what it takes off stuns
(the enemy block's words, as src/formats/scenario.cpp reads them). The
demo's scenarios (1 to 7) have no original to follow; their monsters take
the numbers of that kind's first appearance in the original, its weakest
(the usual one of the first scenario it is in).

Kept as they are: kind, place, facing, wander radius, the AI list (ours,
in ai.txt) and the drop list (ours, in drops.txt). Only numbers are taken
from the original.
"""

import collections
import sys
from pathlib import Path

from first_area import OFFSET, find, read_mct

# Named fields after the 18 positional ones: (key, words), written when
# they differ from what the engine assumes without them.
SLOTS = [("attacks", (23, 24, 25)), ("hits", (29, 30, 31)), ("spells", (32, 33, 34)),
         ("spell_power", (35, 36, 37)), ("spell_level", (38, 39, 40)), ("spell_hit", (41, 42, 43)),
         ("stun", (61, 62, 63))]
SINGLE = [("magic_evasion", 12, 0), ("speed", 77, 1000)]
SPELL_KEYS = {"spells", "spell_power", "spell_level", "spell_hit"}
DEFAULT_SLOTS = {"spells": [-1, -1, -1], "spell_power": [0, 0, 0], "spell_level": [1, 1, 1],
                 "spell_hit": [0, 0, 0], "stun": [0, 0, 0]}


def stats_line(line, w):
    """An enemy= line with the numbers of the original's words `w`."""
    body = line.split("#")[0].strip()[len("enemy="):]
    v = [f.strip() for f in body.split(",")][:18]
    if len(v) < 8:
        raise SystemExit(f"too few fields: {line}")
    drops = v[8] if len(v) > 8 else "0"
    out = v[:4] + [w[8], w[23], v[6], v[7], drops, w[49], w[50], w[51], w[13], w[9], w[29], w[10], w[11], w[6]]
    out = [str(x) for x in out]
    casts = any(w[i] != -1 for i in (32, 33, 34))
    for key, words in SLOTS:
        if key in SPELL_KEYS and not casts:
            continue  # the spell slots mean nothing without a spell
        values = [w[i] for i in words]
        implied = DEFAULT_SLOTS.get(key, [values[0]] * 3)
        if values != implied:
            out.append(f"{key}=" + "/".join(str(x) for x in values))
    for key, word, default in SINGLE:
        if w[word] != default:
            out.append(f"{key}={w[word]}")
    if w[7] == 100:  # only 100 matters (Counter Burst); the rest are 1 or 0
        out.append("word7=100")
    return "enemy=" + ",".join(out)


def main():
    game, out = Path(sys.argv[1]), Path(sys.argv[2])
    if not (game / "Scenario").is_dir():
        game = next(p.parent for p in game.rglob("Scenario") if (p.parent / "Map").is_dir())
    kinds = {int(p.name) for p in (out / "Character" / "ENEMY").iterdir()}

    originals = {}
    first = {}  # each kind's first appearance, the story's scenarios in order
    for sc in sorted((game / "Scenario").iterdir()):
        mct = find(sc, "Scenario", ".mct")
        if not mct:
            continue
        enemies = read_mct(mct.read_bytes())[2][2]
        originals[int(sc.name)] = enemies
        if not sc.name.startswith("99"):  # 99xxxxxx are not the story's
            here = collections.defaultdict(collections.Counter)
            for c in enemies:
                here[c["kind"]][tuple(c["words"])] += 1
            for kind, counts in here.items():  # its usual one there, not a leader
                first.setdefault(kind, list(counts.most_common(1)[0][0]))

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
                words = theirs[n]["words"]
            else:
                words = first[kind]
            lines[i] = stats_line(lines[i], words)
        f.write_text("\n".join(lines))
        print(f"{f.parent.name}: {len(ours)} monsters" + (" (as the original's)" if theirs else " (as each kind first appears)"))


if __name__ == "__main__":
    main()
