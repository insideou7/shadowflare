#!/usr/bin/env python3
"""Gives the open pack's character charts the original's timing, without
rendering anything again. Needs the player's own copy of the game.

    python3 tools/art/retime.py <game folder> <content pack>

The original shows one frame of a chart per tick, 30 a second, and its
charts are long: a goblin's attack is 40 frames with the blow on frame 14,
where ours was rendered with 8. A monster's next move waits for its
attack to end, so a short chart makes it strike far more often than the
original's. This stretches each of our rendered charts over the
original's frame count by showing frames for several ticks, and marks the
original's strike frames ("!", the 0x40 flag), so attacks, bites, hits
and deaths last as long as the original's and land when they do:

- a chart that plays once (hit, death, attack, casting, bite) and that
  the original character also has takes its length and strike frames;
  our strike pose (the frame marked "!", else halfway through a monster's
  attack or a companion's bite, where the engine strikes without a mark)
  is put on the original's first strike frame;
- every other chart (idle and walk loops, whose pace goes with the
  render, and charts the original lacks) is stretched by 1.5 so it plays
  as it did when the engine showed a frame every 20th of a second.

Only numbers (frame counts, strike frames) come from the original. It
works on Character/ENEMY, PARTNER and PEOPLE and Player/Male and Female,
by folder number. A stretched chart gets a "# retimed" line, so running
it again (after a chart is rendered again, say) starts from the rendered
frames, not the stretched ones. The sheets are untouched.
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "re"))
import caf  # noqa: E402
import rclib  # noqa: E402

FOLDERS = [("Character/ENEMY", "Animation"), ("Character/PARTNER", "Animation"),
           ("Character/PEOPLE", "Animation")]
PLAYERS = ["Player/Male", "Player/Female"]
LOOP_STRETCH = 1.5  # the original's 30 frames a second over our old 20
HALFWAY = {"Character/ENEMY": (4, 5, 6), "Character/PARTNER": (5,)}  # struck halfway without a mark
MARK = "# retimed "


def find(folder, name):
    """A file in a folder of the original's, whatever its case."""
    if not folder.is_dir():
        return None
    for p in folder.iterdir():
        if p.name.lower() == name.lower():
            return p
    return None


def original_charts(path):
    """Chart number -> (frames, strike frames) of the body in direction 0."""
    data = path.read_bytes()
    if data[:8] == rclib.MAGIC:
        data = rclib.unpack_at(data, 0)[0]
    out = {}
    for number, chart in enumerate(caf.parse(data)["charts"]):
        layers = chart["dirs"][0]["layers"]
        if not layers or not layers[0] or all(c["pattern"] < 0 for c in layers[0]):
            continue
        out[number] = (len(layers[0]), [j for j, c in enumerate(layers[0]) if c["status"] & 0x40])
    return out


def mapping(n, big, anchor_old, anchor_new):
    """For each of `big` new frames, the old frame (0..n-1) it shows: spread
    evenly, with old frame `anchor_old` starting at new frame `anchor_new`."""
    if anchor_old is None or anchor_new is None:
        return [j * n // big for j in range(big)]
    out = []
    for j in range(big):
        if j < anchor_new:
            out.append(j * anchor_old // anchor_new)
        else:
            out.append(anchor_old + (j - anchor_new) * (n - anchor_old) // (big - anchor_new))
    return out


def plain(token):
    return token.replace("!", "")


def struck(token):
    """The token with the strike mark, right after the frame number."""
    cut = min([i for i in (token.find("%"), token.find("@")) if i >= 0], default=len(token))
    return token[:cut] + "!" + token[cut:]


def is_body(key):
    """Whether a line is the body's (layer 0), whose cells strikes are read from."""
    target = key.split("@")[0]
    return "." not in target or target.split(".")[1] == "0"


class Chart:
    def __init__(self, header):
        self.header = header
        m = re.match(r"\[chart (\d+) (loop|once)\]", header)
        self.number, self.loop = int(m.group(1)), m.group(2) == "loop"
        self.lines = []  # (key, tokens) or (None, raw text)
        self.marker = None

    def body(self, direction="0"):
        for key, tokens in self.lines:
            if key is not None and re.split(r"[.@]", key)[0] == direction and is_body(key):
                return tokens
        # A chart with no direction 0 (one that faces no way, direction 8).
        return next((t for k, t in self.lines if k is not None and is_body(k)), None)


def read_anim(text):
    head, charts = [], []
    for line in text.split("\n"):
        if line.startswith("[chart"):
            charts.append(Chart(line))
        elif not charts:
            head.append(line)
        elif line.startswith(MARK):
            charts[-1].marker = line
        elif "=" in line and not line.startswith("#"):
            key, value = line.split("=", 1)
            charts[-1].lines.append((key, value.split(",")))
        else:
            charts[-1].lines.append((None, line))
    return head, charts


def write_anim(head, charts):
    out = list(head)
    for c in charts:
        out.append(c.header)
        if c.marker:
            out.append(c.marker)
        out += [f"{k}={','.join(t)}" if k is not None else t for k, t in c.lines]
    return "\n".join(out)


def rendered(chart):
    """The chart as rendered: undoes an earlier retime. Returns the lines'
    tokens with strike marks only where the render put them."""
    if not chart.marker:
        return
    words = dict(w.split("=") for w in chart.marker[len(MARK):].split())
    n, anchor = int(words["rendered"]), int(words["anchor"])
    new_anchor = int(words["at"])
    for i, (key, tokens) in enumerate(chart.lines):
        if key is None:
            continue
        m = mapping(n, len(tokens), None if anchor < 0 else anchor, None if new_anchor < 0 else new_anchor)
        first = {}
        for j, old in enumerate(m):
            first.setdefault(old, j)
        back = [plain(tokens[first[k]]) for k in range(n)]
        if 0 <= anchor and words.get("marked") == "1" and is_body(key):
            back[anchor] = struck(back[anchor])
        chart.lines[i] = (key, back)
    chart.marker = None


def retime(chart, original, folder_kind):
    rendered(chart)
    body = chart.body()
    if not body:
        return False
    n = len(body)
    marked = [j for j, t in enumerate(body) if "!" in t]
    if not chart.loop and original is not None:
        big, strikes = original
        anchor = marked[0] if marked else (n // 2 if chart.number in HALFWAY.get(folder_kind, ()) else None)
        at = strikes[0] if strikes and anchor is not None else None
    else:
        big = round(n * LOOP_STRETCH)
        anchor = marked[0] if marked else None
        at = None if anchor is None else round(anchor * LOOP_STRETCH)
        strikes = [at] if at is not None else []
    if big == n and strikes == marked:
        return False
    for i, (key, tokens) in enumerate(chart.lines):
        if key is None:
            continue
        m = mapping(len(tokens), big, anchor, at)
        new = [plain(tokens[k]) for k in m]
        if is_body(key):
            for s in strikes:
                if s < big:
                    new[s] = struck(new[s])
        chart.lines[i] = (key, new)
    chart.marker = (f"{MARK}rendered={n} anchor={-1 if anchor is None else anchor} "
                    f"at={-1 if at is None else at} marked={1 if marked else 0}")
    return True


def do_file(anim, cafs, kind):
    head, charts = read_anim(anim.read_text())
    changed = []
    for c in charts:
        if retime(c, cafs.get(c.number) if cafs is not None else None, kind):
            changed.append(c.number)
    anim.write_text(write_anim(head, charts))
    return changed


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    game, pack = Path(sys.argv[1]), Path(sys.argv[2])
    jobs = []
    for kind, stem in FOLDERS:
        for folder in sorted((pack / kind).glob("*")):
            anim = folder / f"{stem}.anim"
            if anim.exists():
                jobs.append((anim, find(game / kind / folder.name, f"{stem}.caf"), kind))
    for player in PLAYERS:
        anim = pack / player / "Animation00.anim"
        if anim.exists():
            jobs.append((anim, find(game / player, "Animation00.caf"), player))
    for anim, original, kind in jobs:
        cafs = original_charts(original) if original else None
        changed = do_file(anim, cafs, kind)
        print(f"{anim.relative_to(pack)}: charts {changed}" + ("" if cafs is not None else " (no original)"))


if __name__ == "__main__":
    main()
