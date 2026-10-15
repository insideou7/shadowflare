#!/usr/bin/env python3
"""Put the original's Enemy Holes into the open pack. Needs the player's own
copy of the game, as first_area.py does.

    python3 tools/content/enemy_holes.py <game folder> <content pack> [area ...]

An Enemy Hole is a monster of kind -1 with no picture of its own: a burrow
in the floor, drawn by the objects round it, that brings its monsters back
one at a time while it stands, until the hero breaks it. The original's
dungeon scripts do the reviving. For every hole of a first-area scenario
this writes:

- an enemy=-1 line into scenario.txt, in the original's order among the
  monsters (so the open pack's monster numbers are the original's), with
  the original's numbers (enemy_stats.stats_line),
- three objects into script.txt, all drawn with props of Map/props.sheet:
  the burrow (`hole`), the burrow glowing while it brings one back
  (`hole_open`) and the caved-in burrow once it is broken (`hole_broken`),
- the script that runs it, the same steps as the original's: every 200 of
  the original's ticks, if the hole stands and one of its monsters is dead,
  the burrow glows (at tick 180, for 40 ticks, with the original's effects
  and sound) and that monster climbs back out of it (RESSURECT, at tick
  200). When the hole dies it caves in.

The script goes between the "# Burrows" marker and the end of script.txt,
replacing what an earlier run wrote there; anything above is kept. Our own
words; only the numbers and the order of steps are the original's.

In the later areas (2 on) only the enemy=-1 lines are written: their
scripts, burrows and all, are the original's, translated by
tools/content/scripts.py, run after this.
"""

import re
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import first_area as fa  # noqa: E402
from enemy_stats import stats_line  # noqa: E402

MARKER = "# Burrows (written by tools/content/enemy_holes.py)"
TABLE = HERE.parent.parent / "src" / "logic" / "open_area_table.inc"
# Temp flags the burrows' script uses: 900 + 10 per hole and up.
TEMP_BASE = 900
# The burrows' own top lines, taken out and written again on every run.
TOP = re.compile(r"object=[-\d,]+,hole\d+(-open|-broken)?,|temp=9\d\d,")


def read_scs(data):
    """Temp flag starting values, triggers and sentences of a Scenario.Scs."""
    o = 16

    def i32():
        nonlocal o
        v = struct.unpack_from("<i", data, o)[0]
        o += 4
        return v

    temps = dict((i32(), i32()) for _ in range(i32()))
    n = i32()
    o += 8 * n
    for _ in range(i32()):
        i32()
        n = i32()
        o += n
    triggers = [tuple(i32() for _ in range(4)) for _ in range(i32())]
    sentences = []
    for _ in range(i32()):
        commands = []
        for _ in range(i32()):
            op, n = i32(), i32()
            commands.append((op, [(i32(), i32()) for _ in range(n)]))
        sentences.append(commands)
    return temps, triggers, sentences


def offsets():
    """Original scenario -> the original world point at our map's corner."""
    out = {}
    for line in TABLE.read_text().split("\n"):
        m = re.match(r"\{(-?\d+), (-?\d+), (-?\d+),", line)
        if m:
            out[int(m.group(1))] = (int(m.group(2)), int(m.group(3)))
    return out


def hole_steps(scs, hole):
    """What the original's script does for the hole with enemy id `hole`:
    its monsters (first, last), its tick numbers (wake, revive, glow), the
    sound and effects as it wakes, and the sound as it dies."""
    temps, triggers, sentences = scs
    value = lambda t, v: temps.get(v, 0) if t == 4 else v  # noqa: E731
    game_id = 14000000 + hole
    found = {}
    for commands in sentences:
        for op, ops in commands:
            if op == 32 and ops[0][1] == game_id + 1:  # CHECKENEMYDEAD first last var
                found["range"] = (ops[0][1], ops[1][1])
    # The tick sentence: IF counter = wake, IF counter = revive, IF eliv hole.
    for commands in sentences:
        ifs = [ops for op, ops in commands if op == 0]
        if len(ifs) >= 3 and ifs[2][0] == (3, hole) and ifs[0][0][0] == 4:
            found["wake"], found["revive"] = ifs[0][2][1], ifs[1][2][1]
            wake = sentences[ifs[0][3][1]]
            # wake -> IF eliv -> CHECKENEMYDEAD; IF found -> show, SETFLAG glow N, ROUTIN
            for s in [ifs[0][3][1]] + [ops[3][1] for op, ops in wake if op == 0]:
                for op, ops in sentences[s]:
                    if op == 0 and ops[3][1] < len(sentences):
                        for op2, ops2 in sentences[ops[3][1]]:
                            if op2 == 1 and ops2[1][0] == 1 and ops2[0][0] == 4:
                                found["glow"] = ops2[1][1]
    for _, event, character, s in triggers:
        if event == 6 and character == hole:  # ROUTIN: the effects as it wakes
            found["effects"] = [ops[0][1] for op, ops in sentences[s] if op == 36]
            found["wake_sound"] = [value(*ops[0]) for op, ops in sentences[s] if op == 16][0]
        if event == 4 and character == game_id:
            found["dead_sound"] = [value(*ops[0]) for op, ops in sentences[s] if op == 16][0]
    return found


def script_for(holes):
    """Our script.txt lines for the holes: (number, x, y, steps) each. Gives
    the objects and starting flags, which must come before every sentence of
    the script, and the section of sentences."""
    out = [MARKER, "#",
           "# Each burrow brings its monsters back, one at a time, while it",
           "# stands: every 200 ticks, if one of them is dead, the burrow glows",
           "# and it climbs back out. Breaking the burrow ends it. temp:<n> flags",
           f"# from {TEMP_BASE} on: the tick count, the glow left, the monster",
           "# coming back, whether the burrow is still standing, set up.", ""]
    # Objects and starting flags come before the sentences.
    top = []
    for k, (number, x, y, f) in enumerate(holes):
        t = TEMP_BASE + 10 * k
        count, glow, back, standing, ready = (f"temp:{t + i}" for i in range(5))
        name = f"hole{k}"
        enemy = 14000000 + number
        first, last = f["range"]
        rect = f"{x - 80},{y - 80},{x + 80},{y + 80}"
        top += [f"object={rect},{name},hole,hidden",
                f"object={rect},{name}-open,hole_open,hidden",
                f"object={rect},{name}-broken,hole_broken,hidden",
                f"temp={t + 1},-1",
                f"temp={t + 2},-1"]
        out += [f"[sentence {name}-start]", "on=start", f"IF {ready} = 0 @{name}-setup", "",
                f"[sentence {name}-setup]", f"SETFLAG {ready} 1", f"SETFLAG shown:{name} 1", "",
                f"# Monsters {first} to {last} are this burrow's.",
                f"[sentence {name}-tick]", "on=tick",
                f"CHECKENEMYLIVE {enemy} {enemy} {standing}",
                f"IF {count} = {f['wake']} @{name}-wake",
                f"IF {count} = {f['revive']} @{name}-revive",
                f"IF {standing} ! -1 @{name}-glow",
                f"ADD {count} 1", "",
                f"[sentence {name}-wake]", f"IF {standing} ! -1 @{name}-look", "",
                f"[sentence {name}-look]", f"CHECKENEMYDEAD {first} {last} {back}",
                f"IF {back} ! -1 @{name}-open", "",
                f"[sentence {name}-open]", f"SETFLAG shown:{name} 0", f"SETFLAG shown:{name}-open 1",
                f"SETFLAG {glow} {f['glow']}"]
        out += [f"CREATEEFFECT {e} x:{name} y:{name} 0 -1" for e in f["effects"]]
        out += [f"PLAYSOUND {f['wake_sound']} 0 x:{name} y:{name}", "",
                f"[sentence {name}-revive]", f"IF {back} ! -1 @{name}-bring", f"SETFLAG {count} 0", "",
                f"[sentence {name}-bring]", f"IF {standing} ! -1 @{name}-rise", "",
                f"[sentence {name}-rise]", f"RESSURECT {back} x:{name} y:{name} 7", "",
                f"[sentence {name}-glow]", f"IF {glow} > -1 @{name}-fade", "",
                f"[sentence {name}-fade]", f"IF {glow} = 0 @{name}-shut", f"SUB {glow} 1", "",
                f"[sentence {name}-shut]", f"SETFLAG shown:{name} 1", f"SETFLAG shown:{name}-open 0", "",
                f"[sentence {name}-broken]", f"on=enemy-dead {enemy}",
                f"PLAYSOUND {f['dead_sound']} 0 x:{name} y:{name}",
                f"SETFLAG shown:{name} 0", f"SETFLAG shown:{name}-open 0", f"SETFLAG shown:{name}-broken 1", ""]
    return top, out


def main():
    game, out = Path(sys.argv[1]), Path(sys.argv[2])
    if not (game / "Scenario").is_dir():
        game = next(p.parent for p in game.rglob("Scenario") if (p.parent / "Map").is_dir())
    corner = offsets()
    areas = [int(a) for a in sys.argv[3:]] or list(fa.AREAS)
    for number in (n for a in areas for n in fa.AREAS[a]):
        sc = game / "Scenario" / f"{number:08d}"
        enemies = fa.read_mct(fa.find(sc, "Scenario", ".mct").read_bytes())[2][2]
        if not any(c["kind"] == -1 for c in enemies):
            continue
        scs = read_scs(fa.find(sc, "Scenario", ".scs").read_bytes())
        x0, y0 = corner[number]
        folder = out / "Scenario" / f"{fa.our_number(number):08d}"
        lines = (folder / "scenario.txt").read_text().split("\n")
        # Take out an earlier run's holes, then put them back in the
        # original's order among the monsters.
        lines = [line for line in lines if not line.startswith("enemy=-1,")]
        at = [i for i, line in enumerate(lines) if line.startswith("enemy=")]
        ours = [lines[i] for i in at]
        others = [c for c in enemies if c["kind"] != -1]
        if len(others) != len(ours):
            raise SystemExit(f"{folder}: {len(ours)} monsters but the original has {len(others)}")
        merged, holes, theirs = [], [], iter(ours)
        for n, c in enumerate(enemies):
            if c["kind"] != -1:
                merged.append(next(theirs))
                continue
            x, y = c["x"] - x0, c["y"] - y0
            steps = hole_steps(scs, c["id"])
            # The drop list of the monsters it brings back.
            first = steps["range"][0] - 14000000
            mate = next(i for i, o in enumerate(enemies) if o["id"] == first)
            drops = ours[mate - sum(1 for o in enemies[:mate] if o["kind"] == -1)].split(",")[8]
            base = f"enemy=-1,{x},{y},{c['direction'] % 8},0,0,0,none,{drops}"
            merged.append(stats_line(base, c["words"]))
            # The monsters it brings back, by our numbers (the same as the
            # original's place in the list, now the holes are in it).
            index = {o["id"]: i for i, o in enumerate(enemies)}
            lo, hi = (index[v - 14000000] for v in steps["range"])
            steps["range"] = (14000000 + lo, 14000000 + hi)
            holes.append((n, x, y, steps))
        lines[at[0]:at[-1] + 1] = merged
        (folder / "scenario.txt").write_text("\n".join(lines))
        if number not in fa.AREAS[1]:
            # scripts.py translates the later areas' whole scripts, their
            # burrows' steps and pieces among them.
            print(f"{folder.name}: {len(holes)} burrows")
            continue

        path = folder / "script.txt"
        text = path.read_text() if path.exists() else ""
        if MARKER in text:
            text = text[:text.index(MARKER)]
        lines = [line for line in text.rstrip("\n").split("\n") if not TOP.match(line)]
        if lines == [""]:
            lines = []
        top, section = script_for(holes)
        # The objects go in above the script's first section.
        at = next((i for i, line in enumerate(lines) if line.startswith("[")), len(lines))
        while at > 0 and lines[at - 1].startswith("#"):
            at -= 1
        lines[at:at] = top + [""]
        path.write_text("\n".join(lines + [""] + section))
        print(f"{folder.name}: {len(holes)} burrows: " +
              "; ".join(f"#{n} at {x},{y} brings back {s['range'][0] - 14000000}-{s['range'][1] - 14000000}"
                        for n, x, y, s in holes))


if __name__ == "__main__":
    main()
