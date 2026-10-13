"""The eight pieces of the open pack's music, written for this project.

Each one stands where the original plays a piece (docs/sound.md, Music):
the title screen and the seven kinds of area. They share only their
purpose with the original's music: the tunes, keys and instruments are
our own.
"""

from music import Piece, arpeggio, chords_per_bar, drums, pad, render, roots  # noqa: F401


def title():
    """The title screen: a slow, solemn call to adventure in D minor."""
    p = Piece("title", 50, "aeolian", 72, 4, 8)  # D3
    ch = chords_per_bar(p, [1, 6, 7, 1, 4, 6, (9, 13, 16), (9, 13, 16)])
    p.part("strings", pad(p, ch, 0, 0.35), -0.3, 0.4)
    p.part("lowstrings", roots(p, ch, -1, 0.5), 0.2, 0.3)
    p.part("harp", arpeggio(p, ch, 1, 0.35, [0, 1, 2, 3, 2, 1], 0.5), 0.5, 0.35)
    tune, _ = p.melody("1/2 5/2 | 3/1.5 2/0.5 1/2 | 2/2 7,/2 | 1/4 | 4/2 6/2 | 5/2 3/1 4/1 | 5/3 4/1 | 5/4", octave=1, vel=0.7)
    p.part("horn", tune, 0.0, 0.35)
    p.part("taiko", drums(p, [(0, 1.0), (2, 0.5), (3.5, 0.35)], 0.7), 0.0, 0.25)
    p.part("gong", [(0, 4, None, 0.5)], 0.0, 0.4)
    return p


def town():
    """Towns: a cheerful country dance in G major, three beats to a bar."""
    p = Piece("town", 55, "major", 108, 3, 36)  # G3
    a = [1, 1, 4, 1, 6, 4, 5, 5]
    a2 = [1, 1, 4, 1, 6, 4, 5, 1]
    b = [4, 5, 3, 6, 2, 5, 1, 1]
    tag = [4, 5, 1, 1]
    ch = chords_per_bar(p, a + a2 + b + a2 + tag)
    tune_a = "5 3 5 | 1'/2 7 | 6 4 6 | 5/3 | 3 1 3 | 4 6 4 | 5 2' 7 | 5/3 "
    tune_a2 = "5 3 5 | 1'/2 7 | 6 4 6 | 5/3 | 3 1 3 | 4 6 4 | 2' 7 5 | 1'/3 "
    tune_b = "1'/2 6 | 7/2 5 | 5 7 5 | 3/3 | 2 4 6 | 5 7 2' | 3'/2 2' | 1'/3 "
    tune_tag = "6/3 | 7/3 | 1'/3 | r/3"
    tune, beats = p.melody(tune_a + tune_a2 + tune_b + tune_a2 + tune_tag, octave=1, vel=0.75)
    assert beats == p.beats, beats
    p.part("flute", tune, 0.15, 0.3)
    # the dance: a bass note on the first beat and the chord on the other two
    p.part("bass", roots(p, ch, -1, 0.55, [(0, 1, 0)]), -0.1, 0.15)
    p.part("lute", arpeggio(p, ch, 0, 0.3, [3, 1, 2, 4, 1, 2], 0.5), -0.4, 0.25)
    p.part("strings", pad(p, ch[16:24], 0, 0.2), 0.4, 0.4)  # strings under the middle tune
    p.part("frame", drums(p, [(0, 0.8)], 0.5), 0.0, 0.15)
    p.part("tek", drums(p, [(1, 0.5), (2, 0.5), (2.5, 0.3)], 0.5), 0.3, 0.15)
    return p


def fields():
    """Fields: an open, wandering tune in D dorian over rolling harp."""
    p = Piece("fields", 50, "dorian", 84, 4, 20)  # D3
    a = [1, 4, 1, 4, 7, 3, 4, 4]
    b = [3, 7, 1, 5, 3, 7, 4, 1]
    c = [1, 4, 3, 1]
    ch = chords_per_bar(p, a + b + c)
    tune, beats = p.melody(
        "1/1.5 2/0.5 3 5 | 4/2 6 5 | 3/1.5 1/0.5 5/2 | 6/3 r | 7/1.5 6/0.5 5 3 | 3/2 5 1' | 2'/1.5 1'/0.5 7 6 | 4/3 r |"
        "5/2 3 5 | 7/2 6 4 | 5/1.5 4/0.5 3 1 | 2/3 r | 3 5 1' 5 | 7/1.5 1'/0.5 2'/2 | 2' 1' 7 6 | 1'/4 |"
        "5/2 4 3 | 4/2 2/2 | 3 5 7/2 | 1/4", octave=1, vel=0.75)
    assert beats == p.beats, beats
    p.part("flute", tune, 0.2, 0.35)
    p.part("harp", arpeggio(p, ch, 0, 0.3, [0, 1, 2, 3, 4, 3, 2, 1], 0.5), -0.35, 0.35)
    p.part("strings", pad(p, ch, 0, 0.17), 0.35, 0.45)
    p.part("bass", roots(p, ch, -1, 0.5, [(0, 2, 0), (2, 2, 0)]), -0.1, 0.2)
    p.part("frame", drums(p, [(0, 0.6), (2.5, 0.35)], 0.4), 0.0, 0.2)
    p.part("shaker", drums(p, [(k * 0.5, 0.6 if k % 2 else 0.35) for k in range(8)], 0.35, start_bar=8), 0.4, 0.1)
    return p


def caravan():
    """The Caravan: a travelling song in E with the bright second and third
    of a desert road, over a drum pattern and a drone."""
    p = Piece("caravan", 52, "hijaz", 96, 4, 24)  # E3
    a = [1, 1, 2, 1, 4, 7, 2, 1]
    b = [4, 4, 1, 1, 7, 2, 1, 1]
    ch = chords_per_bar(p, a + b + a)
    tune_a = "5 4/0.5 3/0.5 2 1 | 3/1.5 4/0.5 5/2 | 6 5/0.5 4/0.5 2/2 | 3/0.5 2/0.5 1/3 | 4 5 6 1' | 7/1.5 6/0.5 4/2 | 6 5/0.5 4/0.5 3 2 | 1/4 |"
    tune_b = "1'/2 7 6 | 5 6 4/2 | 5/1.5 3/0.5 1/2 | 2 3 5/2 | 7/2 6 5 | 6 4 2/2 | 3 2 3 5 | 1/4 |"
    tune, beats = p.melody(tune_a + tune_b + tune_a, octave=1, vel=0.75)
    assert beats == p.beats, beats
    p.part("ney", tune, 0.1, 0.35)
    p.part("dulcimer", arpeggio(p, ch, 0, 0.28, [0, 1, 2, 1, 3, 2, 1, 2], 0.5), -0.4, 0.25)
    p.part("bass", roots(p, ch, -1, 0.5, [(0, 1.5, 0), (1.5, 1, 0), (3, 1, 1)]), 0.0, 0.15)
    p.part("drone", [(0, p.beats, 40, 0.18), (0, p.beats, 47, 0.12)], 0.0, 0.3)
    # the doum and tek of a travelling rhythm
    p.part("frame", drums(p, [(0, 1.0), (2, 0.8)], 0.6), -0.1, 0.15)
    p.part("tek", drums(p, [(0.5, 0.6), (1.5, 0.7), (3, 0.7), (3.5, 0.4)], 0.55), 0.2, 0.15)
    p.part("shaker", drums(p, [(k * 0.5, 0.6 if k % 2 else 0.3) for k in range(8)], 0.3), 0.45, 0.1)
    return p


def boss():
    """Boss rooms: a driving fight in C harmonic minor."""
    p = Piece("boss", 48, "harmonic", 140, 4, 32)  # C3
    a = [1, 1, 6, 6, 4, 4, 5, 5]
    b = [1, 6, 4, 5, 1, 6, 2, 5]
    ch = chords_per_bar(p, a + b + a + b)
    tune_a = "5/2 3/2 | 1'/3 7 | 1'/2 3'/2 | 1'/4 | 6/2 4/2 | 1'/2 6/2 | 7/2 2'/2 | 5'/2 7/2 |"
    tune_b = "5'/2 3'/1 2'/1 | 1'/2 3'/2 | 4'/2 3'/1 1'/1 | 7/4 | 3'/2 5'/2 | 3'/2 1'/2 | 4'/2 2'/2 | 7/2 5/2 |"
    tune, beats = p.melody(tune_a + tune_b + tune_a + tune_b, octave=1, vel=0.7)
    assert beats == p.beats, beats
    p.part("strings", tune, 0.2, 0.3)
    p.part("horn", [n for n in tune if n[0] >= 64], -0.2, 0.3)  # brass joins the second time
    ost = []
    for at, beats_, spec in ch:
        r = p.chord(spec, -1)[0]
        for k, off in enumerate([0, 0, 12, 0, 7, 0, 12, 7]):
            ost.append((at + k * 0.5, 0.45, r + off, 0.7 if k == 0 else 0.5))
    p.part("lowstrings", ost, -0.3, 0.2)
    p.part("horn", [(at, 1.0, m, 0.5) for at, _, spec in ch[::2] for m in p.chord(spec, 0)], 0.3, 0.3)
    p.part("taiko", drums(p, [(0, 1.0), (1.5, 0.6), (2, 0.8), (3, 0.6), (3.5, 0.5)], 0.8), 0.0, 0.2)
    p.part("snare", drums(p, [(1, 0.5), (3, 0.5)], 0.45), 0.2, 0.2)
    p.part("gong", [(0, 4, None, 0.5), (64, 4, None, 0.5)], 0.0, 0.4)
    return p


def ruins():
    """Ruins, towers and forts: a slow, wary march in A minor."""
    p = Piece("ruins", 45, "aeolian", 76, 4, 18)  # A2
    a = [1, 6, 3, 7, 1, 6, 4, 5]
    b = [4, 4, 1, 1, 6, 7, 1, 1]
    ch = chords_per_bar(p, a + b + [1, 1])
    tune, beats = p.melody(
        "1/2 3 5 | 6/3 5 | 3/2 5/2 | 4/2 2/2 | 5/2 1'/2 | 6/2 1' 6 | 4/2 6 4 | 5/4 |"
        "6/2 4/2 | 1'/3 6 | 5/2 3/2 | 1/4 | 3/2 6/2 | 7/2 2'/2 | 1'/4 | 5/4 | r/8", octave=1, vel=0.65)
    assert beats == p.beats, beats
    p.part("horn", tune, 0.1, 0.4)
    p.part("lowstrings", pad(p, ch, -1, 0.2), -0.3, 0.4)
    p.part("oo", pad(p, ch, 0, 0.16), 0.3, 0.5)
    p.part("harp", arpeggio(p, ch, 0, 0.25, [0, 2, 1, 3], 1.0), -0.5, 0.4)
    p.part("bell", [(at, 4, p.chord(spec, 1)[0], 0.25) for at, _, spec in ch[::4]], 0.4, 0.5)
    p.part("taiko", drums(p, [(0, 0.8), (2, 0.6)], 0.6), 0.0, 0.3)
    p.part("snare", drums(p, [(1, 0.3), (3, 0.3), (3.5, 0.2), (3.75, 0.2)], 0.4), 0.15, 0.3)
    return p


def caves():
    """Caves: dark and still, a drone, far-off notes and water dripping."""
    p = Piece("caves", 40, "aeolian", 60, 4, 14)  # E2
    ch = chords_per_bar(p, [[1, 2], [6, 2], [4, 2], [1, 2], [7, 2], [6, 2], [1, 2]])
    p.part("drone", [(0, p.beats, 28, 0.35), (0, p.beats, 35, 0.2)], 0.0, 0.5)
    p.part("oo", pad(p, ch, 0, 0.12), 0.0, 0.7)
    notes = []
    from synth import rng
    r = rng(5)
    penta = [1, 3, 4, 5, 7]
    at = 1.0
    while at < p.beats - 1:
        notes.append((at, 2, p.degree(int(r.choice(penta)), int(r.integers(1, 3))), r.uniform(0.15, 0.3)))
        at += float(r.choice([2, 3, 4, 5]))
    p.part("bell", notes, 0.3, 0.8)
    harp = []
    for at, beats, spec in ch:
        tones = p.chord(spec, 1)
        harp += [(at + 1, 2, tones[0], 0.2), (at + 1.5, 2, tones[1], 0.15), (at + 2, 2, tones[2], 0.15)]
    p.part("harp", harp, -0.4, 0.7)
    drips = []
    at = 0.3
    while at < p.beats:
        drips.append((at, 0.2, int(r.integers(76, 90)), r.uniform(0.15, 0.35)))
        at += float(r.uniform(1.0, 3.5))
    p.part("drip", drips, 0.5, 0.9)
    return p


def shrine():
    """Shrines: a calm hymn in F with a raised fourth, voices and bells."""
    p = Piece("shrine", 53, "lydian", 66, 4, 16)  # F3
    ch = chords_per_bar(p, [1, 2, 1, 2, 6, 3, 2, 1, 5, 6, 2, 1, 6, 2, 5, 1])
    tune, beats = p.melody(
        "5/4 | 4/2 6/2 | 3/2 1/2 | 2/4 | 6/2 1'/2 | 7/4 | 6/2 4/2 | 5/4 |"
        "5/2 7/2 | 1'/2 6/2 | 2'/2 4'/2 | 3'/4 | 1'/2 6/2 | 6/2 2'/2 | 7/2 5/2 | 1'/4", octave=1, vel=0.6)
    assert beats == p.beats, beats
    p.part("flute", tune, 0.15, 0.5)
    p.part("choir", pad(p, ch, 0, 0.22), -0.1, 0.6)
    p.part("harp", arpeggio(p, ch, 0, 0.25, [0, 1, 2, 3], 1.0), -0.45, 0.5)
    p.part("bell", [(at, 4, p.chord(spec, 2)[0], 0.18) for at, _, spec in ch], 0.45, 0.6)
    p.part("bass", roots(p, ch, -1, 0.35), 0.0, 0.3)
    p.part("gong", [(0, 4, None, 0.35), (32, 4, None, 0.25)], 0.0, 0.5)
    return p


# Where each piece goes in the pack, and which area kind plays it
# (the scenario's third word, docs/sound.md).
PIECES = [
    ("System/Title/Music/BGM00.ogg", title),
    ("System/Game/Music/BGM00.ogg", town),     # 0 towns
    ("System/Game/Music/BGM01.ogg", fields),   # 1 fields
    ("System/Game/Music/BGM02.ogg", caravan),  # 2 the Caravan and two towns
    ("System/Game/Music/BGM03.ogg", boss),     # 3 boss rooms
    ("System/Game/Music/BGM04.ogg", ruins),    # 4 ruins, towers and forts
    ("System/Game/Music/BGM05.ogg", caves),    # 5 caves
    ("System/Game/Music/BGM06.ogg", shrine),   # 6 shrines
]
