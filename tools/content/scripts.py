#!/usr/bin/env python3
"""Write the open pack's scenario scripts after the original's: the same
events, flags, quests, trades and doors, in the same order and with the
same outcomes, but in our own words. Needs the player's own copy of the
game, and first_area.py's maps.

    python3 tools/content/scripts.py <game folder> <content pack> <area> [scenario ...]

For each scenario of the area, the original's Scenario.Scs is turned
into Scenario/<10000000 + number>/script.txt (docs/content.md):

- Its commands and their order are kept. Characters are renamed to the
  pack's: people p<id> (the names first_area.py gives them), objects
  o<id> (written here as object= lines, drawn with the props skin.txt
  gives the original's object pictures), monsters by their place in
  scenario.txt. The NET flags that hold someone's state follow them.
- What is said is ours: every message comes from
  tools/content/words/<our scenario number>.txt, as [message <id>]
  sections. No original text is read into the pack. A message the words
  file lacks is reported and written as "..." so that the gap shows;
  messages that only name the area for the compass (SETCOMPASSES) take the
  scenario's title.
- Exits that scenario.txt already has (an object walked into runs
  CHANGESCENARIO) are left out; exits the original opens only later in
  the story stay here, behind their IF.

Triggers of event 2 (raised only in network play) are left out.
"""

import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import first_area as fa  # noqa: E402
from skin_preview import read_skin  # noqa: E402

NAMES = ["IF", "SETFLAG", "TALK", "TALKPARTNERSTATUS", "IDENTIFY", "TRADE",
         "INITTRADE", "HEALLIFE", "HEALMENTAL", "REPAIR", "CREATEITEM", "ADD",
         "SUB", "MUL", "DIV", "REM", "PLAYSOUND", "CHANGESCENARIO",
         "C_STOP", "C_MOVABLE", "C_ACTIONREQ", "C_SETANGLETO", "SETACTIVE", "SETINACTIVE",
         "CREATEITEMTABLE", "RESSURECT", "MESSAGEBOXVALUE", "MESSAGEBOX", "ROUTIN", "ROUTINNET",
         "CREATEOPTIONANGLE", "CHECKENEMYLIVE", "CHECKENEMYDEAD", "GETNEARPLAYERPOS", "CALCMYPLAYERDIST", "CALCANGLE",
         "CREATEEFFECT", "WARPGATE", "DELETEWARPGATE", "GETRANDAM", "CREATEEFFECTCHAR", "STORAGE",
         "GETPLAYERLIFE", "GETPLAYERMENTAL", "GETPARTNERKIND", "CHANGEPARTNER", "SETCHARTRANS", "GETCHARTRANS",
         "DRAWQUESTNAME", "SETCOMPASSES", "GETENTRYPOINT", "SETMSGDEFINE", "GETREPAIRPRICE", "GETTOTALGOLD",
         "PAYGOLD", "CHECKUNOPINIONITEM", "ABSOLUTEOBJECTFLAG", "GETPLAYERSEX", "CHECKITEMEXIST", "DELETEITEM",
         "SETUNLOCKSW", "GETPLAYERLEVEL", "SETQUESTFLAG", "GETPARTNERLIFE", "STARTVISUAL", "SETTINGRAIN",
         "GETMYPLAYERNO", "MEMORIZEMAGIC", "GETEXPERIENCE", "CHECKMEMORIZEDMAGIC", "ENABLECLASSCHANGE", "GETCLASS",
         "CHANGECOLOR", "STARTCARDGAME", "GETCARDGAMERESULT", "SETITEMDIRECT"]
IF, CHANGESCENARIO, SETCOMPASSES = 0, 17, 49
# Commands whose numbers are items, gold or tables, never characters.
ITEM_COMMANDS = {"CREATEITEM", "CREATEITEMTABLE", "CHECKITEMEXIST", "DELETEITEM", "SETITEMDIRECT", "INITTRADE",
                 "TRADE", "PAYGOLD", "SETQUESTFLAG", "DRAWQUESTNAME", "PLAYSOUND", "STARTVISUAL"}
EVENTS = {0: "talk", 1: "talk-end", 3: "judge", 4: "enemy-dead", 5: "tick", 6: "routine", 7: "start"}
KINDS = {2: "msg", 3: "alive", 4: "temp", 5: "net", 6: "x", 7: "y", 8: "playmode", 9: "held", 10: "warp",
         11: "save", 12: "quest", 13: "exstrg"}
OPERATORS = {0: "=", 1: "!", 2: ">", 3: "<"}
OBJECTS, PEOPLE, ENEMIES = 10000000, 12000000, 14000000


def read_mct(data):
    """Placements of a Scenario.Mct with their states and object look
    words, as src/formats/scenario.cpp reads them."""
    o = 16

    def i32():
        nonlocal o
        v = struct.unpack_from("<i", data, o)[0]
        o += 4
        return v

    o += 0x104 * 2
    i32(), i32(), i32()
    o += 0x100
    for _ in range(3):
        n = i32()
        o += 4 * n
    groups = []
    for tail in (13, 11, 79, 4):
        group = []
        for _ in range(i32()):
            c = dict(id=i32(), kind=i32())
            n = i32()
            if n:
                o += n
                i32()
            i32()
            c["x"], c["y"] = i32(), i32()
            c["rect"] = [i32() for _ in range(4)]
            i32()
            states = [i32() for _ in range(i32())]
            c["states"] = (states + [1, 1, 1])[:3]
            if i32() == 1:
                n = i32()
                o += 10 * n
            i32()
            c["words"] = [i32() for _ in range(tail)]
            group.append(c)
        groups.append(group)
    return groups


def read_scs(data):
    """TEMP flags, message ids, triggers and sentences of a Scenario.Scs
    (the message texts are skipped, never read)."""
    o = 16

    def i32():
        nonlocal o
        v = struct.unpack_from("<i", data, o)[0]
        o += 4
        return v

    temp = [(i32(), i32()) for _ in range(i32())]
    n = i32()
    o += 8 * n
    messages = []
    for _ in range(i32()):
        messages.append(i32())
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
    return temp, messages, triggers, sentences


def read_words(path):
    """[message <id>] sections of a words file: id -> its lines."""
    words, current = {}, None
    if not path.exists():
        return words
    for line in path.read_text().split("\n"):
        if line.startswith("[message ") and line.rstrip().endswith("]"):
            current = int(line.strip()[9:-1])
            words[current] = []
        elif line.startswith("#") and current is None:
            continue
        elif current is not None:
            words[current].append(line)
    return {k: "\n".join(v).rstrip("\n").split("\n") for k, v in words.items()}


def ours(scenario):
    """What scenario.txt places: the number of objects (exits and lamps),
    the people's names and the number of monsters."""
    objects, people, enemies = 0, [], 0
    for line in scenario.read_text().split("\n"):
        key = line.split("=", 1)[0]
        if key in ("exit", "lamp"):
            objects += 1
        elif key == "person":
            f = line.split("#")[0].split("=", 1)[1].split(",")
            people.append(f[4].strip() if len(f) > 4 else None)
        elif key == "enemy":
            enemies += 1
    return objects, people, enemies


def translate(game, pack, number, props, words_dir):
    sc = game / "Scenario" / f"{number:08d}"
    path, title, *_ = fa.read_mct(fa.find(sc, "Scenario", ".mct").read_bytes())
    groups = read_mct(fa.find(sc, "Scenario", ".mct").read_bytes())
    scs = fa.find(sc, "Scenario", ".scs")
    if not scs:
        return
    temp, message_ids, triggers, sentences = read_scs(scs.read_bytes())
    folder = pack / "Scenario" / f"{fa.our_number(number):08d}"
    n_objects, people, n_enemies = ours(folder / "scenario.txt")
    words = read_words(words_dir / f"{fa.our_number(number)}.txt")

    # Where our map lies on the original's (first_area.py's crop).
    o = fa.Original(game, number)
    x0, y0, _, _ = fa.crop(o, fa.walkable(o))

    # Exits scenario.txt has: objects whose judge sentence runs CHANGESCENARIO.
    exits = set()
    for _, event, character, s in triggers:
        if event == 3 and 0 <= s < len(sentences) and any(op == CHANGESCENARIO for op, _ in sentences[s]):
            exits.add(character)
    written = {(l, t, r, b) for l, t, r, b, *_ in fa.written_exits(o, number)}
    by_id = {OBJECTS + c["id"]: c for c in groups[0]}

    def area(c):
        r = c["rect"]
        return c["x"] + r[0], c["y"] + r[1], c["x"] + r[2], c["y"] + r[3]

    skipped = {cid for cid in exits if cid in by_id and area(by_id[cid]) in written}
    skip_sentences = {s for _, event, character, s in triggers if event == 3 and character in skipped}

    # Our in-game ids and names.
    names, numbers = {}, {}
    objects = [c for c in groups[0] if OBJECTS + c["id"] not in skipped]
    for k, c in enumerate(objects):
        names[OBJECTS + c["id"]] = f"o{c['id']}"
        numbers[OBJECTS + c["id"]] = OBJECTS + n_objects + k
    kinds = {int(p.name) for p in (pack / "Character" / "PEOPLE").iterdir()}
    kinds |= {1000000 + int(p.name) for p in (pack / "Character" / "PARTNER").iterdir()}
    ppl = [c for c in groups[1] if c["kind"] in kinds]
    for k, c in enumerate(ppl):
        names[PEOPLE + c["id"]] = people[k]
        numbers[PEOPLE + c["id"]] = PEOPLE + k
    for k, c in enumerate(groups[2]):
        numbers[ENEMIES + c["id"]] = ENEMIES + k
    if len(groups[2]) != n_enemies or len(ppl) != len(people):
        raise SystemExit(f"{number}: scenario.txt places {n_enemies} monsters and {len(people)} people, "
                         f"the original {len(groups[2])} and {len(ppl)}")

    missing = []

    def character(v):
        if v in names and names[v]:
            return names[v]
        if v in numbers:
            return str(numbers[v])
        return str(v)

    def operand(t, v, command, index):
        if t == 0:
            if command == "IF" and index == 1:
                return OPERATORS[v]
            if command == "IF" and index == 3:
                return f"@s{v}"
            return str(v)
        if t == 1:
            if command == "CHANGESCENARIO" and index == 0:
                return str(fa.our_number(v))  # the pack's numbering
            if command not in ITEM_COMMANDS and OBJECTS <= v < 15000000:
                return character(v)
            return str(v)
        if t == 5 and v >= 100000000:
            base, cid = v // 100000000 * 100000000, v % 100000000
            if base == 100000000 and names.get(cid):
                return f"shown:{names[cid]}"
            return f"net:{base + numbers.get(cid, cid)}"
        if t in (6, 7, 9):
            return f"{KINDS[t]}:{character(v)}"
        if t == 3:
            return f"alive:{numbers.get(ENEMIES + v, ENEMIES + v) - ENEMIES}"
        if t == 2:
            used.add(v)
        return f"{KINDS[t]}:{v}"

    used, compass = set(), set()
    body = []
    for i, commands in enumerate(sentences):
        body.append(f"\n[sentence s{i}]")
        for _, event, ch, s in triggers:
            if s != i or event not in EVENTS or i in skip_sentences:
                continue
            if event in (5, 7) and ch == -1:
                body.append(f"on={EVENTS[event]}")
            elif event == 6:
                body.append(f"on=routine {ch}")
            else:
                body.append(f"on={EVENTS[event]} {character(ch)}")
        for op, operands in commands:
            name = NAMES[op]
            body.append(" ".join([name] + [operand(t, v, name, k) for k, (t, v) in enumerate(operands)]))
            if op == SETCOMPASSES and operands and operands[0][0] == 2:
                compass.add(operands[0][1])

    head = [f"# Written by tools/content/scripts.py after the original's scenario {number}",
            f"# ({title}), in our own words (tools/content/words/{fa.our_number(number)}.txt).", ""]
    for c in objects:
        r = c["rect"]
        left, top = c["x"] + r[0] - x0, c["y"] + r[1] - y0
        right, bottom = c["x"] + r[2] - x0, c["y"] + r[3] - y0
        fields = [str(left), str(top), str(right), str(bottom), f"o{c['id']}"]
        w = c["words"]
        prop = None
        if w[0] == 1:
            pieces = props.get(f"object/{c['kind']:08d}", {}).get(w[1])
            prop = pieces[0][0] if pieces else None
        if prop:
            fields.append(prop)
        fields.append("states=" + "/".join(str(1 if s else 0) for s in c["states"]))
        fields.append(f"at={c['x'] - x0}/{c['y'] - y0}")
        head.append("object=" + ",".join(fields))
    for k, v in temp:
        if v:
            head.append(f"temp={k},{v}")
    messages = []
    for mid in message_ids:
        if mid in words:
            text = words[mid]
        elif mid in compass:
            text = [title.strip()]
        else:
            text = ["..."]
            missing.append(mid)
        messages.append(f"\n[message {mid}]\n" + "\n".join(text))
    (folder / "script.txt").write_text("\n".join(head) + "\n" + "".join(m + "\n" for m in messages)
                                       + "\n".join(body) + "\n")
    print(f"{fa.our_number(number)} {title}: {len(sentences)} sentences, {len(objects)} objects, "
          f"{len(message_ids)} messages" + (f", {len(missing)} without words: {missing}" if missing else ""))
    return missing


def main():
    game, pack, area = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
    if not (game / "Map").is_dir():
        game = next(p.parent for p in game.rglob("Map") if (p / "Pattern").is_dir())
    only = {int(a) for a in sys.argv[4:]}
    _, props = read_skin(pack / "skin.txt")
    words_dir = HERE / "words"
    for number in fa.AREAS[area]:
        if only and number not in only and fa.our_number(number) not in only:
            continue
        translate(game, pack, number, props, words_dir)


if __name__ == "__main__":
    main()
