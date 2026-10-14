#!/usr/bin/env python3
"""Give the open pack's spell casters an AI list that casts their spells,
and its throwers one that throws from afar.

    python3 tools/content/caster_ai.py <content pack>

Each enemy= line with spells= (enemy_stats.py copies them from the
original's monsters) gets one of ai.txt's mage lists, by which slots hold a
spell and whether it bursts round the caster (Hell Fire, Ice Blast; cast
close up) or flies and reaches (cast from further off). Each line with
shot= (a stone or arrow thrown by its first attack) and no spells gets
ai.txt's thrower list. Other lines keep their list. Run it again after
enemy_stats.py.
"""

import sys
from pathlib import Path

BURSTS = {4, 5}  # Hell Fire, Ice Blast


def ai_list(spells):
    """The mage list for a monster's three spell slots (-1 = none)."""
    known = [s for s in spells if s >= 0]
    if spells[0] < 0 or len(known) != sum(1 for s in spells[:len(known)] if s >= 0):
        raise SystemExit(f"spells must fill the slots from the first: {spells}")
    if len(known) == 1:
        return "mage"
    if len(known) == 2:
        return "mage-near-far" if spells[0] in BURSTS else "mage-two"
    return "mage-three-burst-last" if spells[2] in BURSTS and spells[0] not in BURSTS else "mage-three"


def fix_line(line):
    body, sep, comment = line.partition("#")
    fields = [f.strip() for f in body.strip()[len("enemy="):].split(",")]
    named = {f.split("=", 1)[0]: f.split("=", 1)[1] for f in fields[18:] if "=" in f}
    spells = [int(v) for v in named.get("spells", "-1/-1/-1").split("/")]
    if any(s >= 0 for s in spells):
        fields[7] = ai_list(spells)
    elif "shot" in named and named["shot"].split("/")[4] == "0":
        fields[7] = "thrower"
    else:
        return line
    return "enemy=" + ",".join(fields) + (" " + sep + comment if sep else "")


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    pack = Path(sys.argv[1])
    changed = 0
    for path in sorted(pack.glob("Scenario/*/scenario.txt")):
        lines = path.read_text().splitlines(keepends=True)
        out = []
        for line in lines:
            end = "\n" if line.endswith("\n") else ""
            new = fix_line(line.rstrip("\n")) + end if line.startswith("enemy=") else line
            changed += new != line
            out.append(new)
        path.write_text("".join(out))
    print(f"{changed} enemy lines changed")


if __name__ == "__main__":
    main()
