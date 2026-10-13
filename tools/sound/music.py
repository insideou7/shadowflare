"""The open pack's music: eight pieces written for this project, one for
each kind of area the original gives music (towns, fields, the Caravan,
boss rooms, ruins, caves and shrines) and one for the title screen.

Each piece is a score: chords, a melody and accompaniment patterns, played
by instruments made with tools/sound/synth.py. Pieces are rendered as
seamless loops (notes and echoes that run past the end come in again at
the start), because the game loops its music.
"""

import numpy as np

from synth import RATE, adsr, band, expdecay, harmonics, noise, reverb_ir, rng, smooth_noise, sine, VOWELS

MODES = {
    "major": [0, 2, 4, 5, 7, 9, 11],
    "dorian": [0, 2, 3, 5, 7, 9, 10],
    "aeolian": [0, 2, 3, 5, 7, 8, 10],
    "harmonic": [0, 2, 3, 5, 7, 8, 11],
    "lydian": [0, 2, 4, 6, 7, 9, 11],
    "hijaz": [0, 1, 4, 5, 7, 8, 10],
}


def mtof(m):
    return 440.0 * 2 ** ((m - 69) / 12)


# --- Instruments ---------------------------------------------------------------
# Each takes a frequency, a length in seconds, a velocity (0-1) and a seed,
# and returns a mono note (which may ring on past its length).

def _n(dur):
    return int(round(dur * RATE))


def pluck(f, dur, vel, seed, bright=1.0, ring=1.2):
    """Lute, harp or dulcimer: a string plucked, its high partials dying first."""
    n = _n(min(dur + ring, 4.0))
    t = np.arange(n) / RATE
    out = np.zeros(n)
    for k in range(1, 24):
        fk = f * k * np.sqrt(1 + 0.0004 * k * k)
        if fk > RATE * 0.45:
            break
        amp = (1 / k ** (1.6 - 0.5 * bright)) * (1 if k % 7 else 0.3)
        out += amp * np.sin(2 * np.pi * fk * t + k) * np.exp(-t * (1.2 + 0.9 * k / bright) / ring)
    click = band(noise(min(n, _n(0.01)), seed), 1500, 7000) * 0.05 * bright
    out[: len(click)] += click
    return out * vel * adsr(n, 0.003, 0.05, 1.0, 0.05)


def flute(f, dur, vel, seed, breathy=0.06):
    n = _n(dur + 0.15)
    t = np.arange(n) / RATE
    vib = 1 + 0.006 * np.sin(2 * np.pi * 5.2 * t) * np.clip((t - 0.25) / 0.3, 0, 1)
    tone = harmonics(f * vib, n, [1, 0.28, 0.12, 0.05, 0.02])
    breath = band(noise(n, seed), f * 1.5, f * 6) * breathy
    return (tone + breath) * vel * adsr(n, 0.07, 0.1, 0.85, 0.15)


def strings(f, dur, vel, seed, attack=0.25, bright=2500):
    """A small string section: three players slightly out of step."""
    n = _n(dur + 0.4)
    t = np.arange(n) / RATE
    out = np.zeros(n)
    r = rng(seed)
    count = int(min(30, RATE * 0.45 / f))
    amps = [1 / k ** 1.1 / (1 + (k * f / bright) ** 2) for k in range(1, count + 1)]
    for d in (-0.004, 0.0, 0.005):
        vib = 1 + d + 0.004 * np.sin(2 * np.pi * r.uniform(4.6, 5.6) * t + r.uniform(0, 6))
        out += harmonics(f * vib, n, amps, r.uniform(0, 6, count))
    return out / 3 * vel * adsr(n, attack, 0.2, 0.9, 0.4)


def choir(f, dur, vel, seed, vowel="ah"):
    """Voices singing a vowel, slowly swelling."""
    n = _n(dur + 0.6)
    t = np.arange(n) / RATE
    count = int(min(40, RATE * 0.45 / f))
    weights = []
    for k in range(1, count + 1):
        fk = k * f
        w = 0.02 + sum(l * np.exp(-0.5 * ((fk - fm) / (bw * 2.5)) ** 2) for fm, bw, l in VOWELS[vowel])
        weights.append(w / k ** 0.6)
    out = np.zeros(n)
    r = rng(seed)
    for d in (-0.006, -0.002, 0.003, 0.007):
        vib = 1 + d + 0.005 * np.sin(2 * np.pi * r.uniform(4.5, 5.8) * t + r.uniform(0, 6))
        out += harmonics(f * vib, n, weights, r.uniform(0, 6, count))
    return out / 4 * vel * adsr(n, 0.6, 0.3, 0.9, 0.6)


def horn(f, dur, vel, seed):
    n = _n(dur + 0.25)
    t = np.arange(n) / RATE
    swell = np.clip(t / 0.12, 0, 1)
    count = int(min(20, RATE * 0.45 / f))
    amps = [(1 / k ** 1.4) * (0.4 + 0.6 * swell) ** (k / 3) for k in range(1, count + 1)]
    vib = 1 + 0.003 * np.sin(2 * np.pi * 5 * t) * np.clip((t - 0.3) / 0.3, 0, 1)
    return harmonics(f * vib, n, amps) * vel * adsr(n, 0.06, 0.15, 0.85, 0.25)


def bell(f, dur, vel, seed):
    n = _n(min(4.0, dur + 2.5))
    t = np.arange(n) / RATE
    out = np.zeros(n)
    for ratio, amp, d in ((1, 1, 2.5), (2.0, 0.45, 1.4), (2.76, 0.3, 0.9), (5.4, 0.15, 0.5), (8.93, 0.06, 0.25)):
        if f * ratio < RATE * 0.45:
            out += amp * np.sin(2 * np.pi * f * ratio * t) * np.exp(-t * 6.9 / d)
    return out * vel * adsr(n, 0.002, 0.05, 1, 0.1)


def bass(f, dur, vel, seed):
    n = _n(dur + 0.2)
    t = np.arange(n) / RATE
    out = harmonics(f, n, [1, 0.5, 0.25, 0.12, 0.06]) * np.exp(-t * 1.5)
    return out * vel * adsr(n, 0.01, 0.1, 0.9, 0.15)


def drone(f, dur, vel, seed):
    n = _n(dur)
    t = np.arange(n) / RATE
    amps = [1, 0.6, 0.4, 0.3, 0.2, 0.15, 0.1, 0.08, 0.05]
    out = harmonics(f * (1 + 0.002 * np.sin(2 * np.pi * 0.3 * t)), n, amps)
    out *= 0.8 + 0.2 * np.sin(2 * np.pi * 0.25 * t + seed)
    return out * vel * adsr(n, 1.0, 0.5, 1, 1.0)


# Drums take no pitch.

def taiko(f, dur, vel, seed):
    n = _n(1.2)
    t = np.arange(n) / RATE
    body = np.sin(2 * np.pi * np.cumsum(60 + 70 * np.exp(-t * 25)) / RATE) * np.exp(-t * 4)
    skin = band(noise(n, seed), 100, 1500) * np.exp(-t * 30) * 0.4
    return (body + skin) * vel


def frame(f, dur, vel, seed, high=False):
    """A frame drum: deep 'doum' or bright 'tek' at the rim."""
    n = _n(0.5)
    t = np.arange(n) / RATE
    if high:
        return band(noise(n, seed), 1500, 6000) * np.exp(-t * 45) * vel * 0.6 + np.sin(2 * np.pi * 420 * t) * np.exp(-t * 40) * vel * 0.3
    return (np.sin(2 * np.pi * np.cumsum(110 + 40 * np.exp(-t * 30)) / RATE) * np.exp(-t * 9) + band(noise(n, seed), 200, 1200) * np.exp(-t * 35) * 0.3) * vel


def shaker(f, dur, vel, seed):
    n = _n(0.12)
    t = np.arange(n) / RATE
    return band(noise(n, seed), 4000, 10000) * np.clip(t / 0.01, 0, 1) * np.exp(-t * 40) * vel * 0.5


def snare(f, dur, vel, seed):
    n = _n(0.35)
    t = np.arange(n) / RATE
    return (band(noise(n, seed), 800, 6000) * np.exp(-t * 18) + np.sin(2 * np.pi * 190 * t) * np.exp(-t * 25) * 0.5) * vel


def gong(f, dur, vel, seed):
    n = _n(4.0)
    t = np.arange(n) / RATE
    out = np.zeros(n)
    r = rng(seed)
    for k in range(12):
        fr = 90 * r.uniform(1, 8)
        out += np.sin(2 * np.pi * fr * t * (1 + 0.003 * np.exp(-t))) * np.exp(-t * r.uniform(0.6, 2.5)) / (k + 1) ** 0.5
    return out * np.clip(t / 0.03, 0, 1) * vel * 0.4


def drip(f, dur, vel, seed):
    """A drop of water falling into a pool."""
    n = _n(0.25)
    t = np.arange(n) / RATE
    return np.sin(2 * np.pi * np.cumsum(f * (1 + 1.5 * t / 0.06).clip(1, 2.5)) / RATE) * np.exp(-t * 30) * vel


INSTRUMENTS = dict(pluck=pluck, flute=flute, strings=strings, choir=choir, horn=horn, bell=bell, bass=bass,
                   drone=drone, taiko=taiko, frame=frame, shaker=shaker, snare=snare, gong=gong, drip=drip,
                   tek=lambda f, d, v, s: frame(f, d, v, s, True),
                   harp=lambda f, d, v, s: pluck(f, d, v, s, 0.8, 2.0),
                   lute=lambda f, d, v, s: pluck(f, d, v, s, 1.2, 0.9),
                   dulcimer=lambda f, d, v, s: pluck(f, d, v, s, 1.8, 1.4),
                   lowstrings=lambda f, d, v, s: strings(f, d, v, s, 0.03, 1800),
                   ney=lambda f, d, v, s: flute(f, d, v, s, 0.14),
                   oo=lambda f, d, v, s: choir(f, d, v, s, "o"))


# --- Scores ----------------------------------------------------------------------

class Piece:
    """A piece: a key, a mode, a tempo and parts, each part a list of
    notes (beat, beats, midi or None for a drum, velocity)."""

    def __init__(self, name, root, mode, bpm, beats_per_bar, bars):
        self.name, self.root, self.scale = name, root, MODES[mode]
        self.bpm, self.bar, self.bars = bpm, beats_per_bar, bars
        self.parts = []  # (instrument, pan -1..1, echo 0..1, notes)

    @property
    def beats(self):
        return self.bar * self.bars

    def degree(self, d, octave=0):
        """A scale degree (1 the root; 8 an octave up, 0 and below go down)."""
        i = d - 1
        return self.root + 12 * (octave + i // 7) + self.scale[i % 7]

    def chord(self, spec, octave=0):
        """The notes of a chord: a scale degree's triad, or semitones over the root."""
        if isinstance(spec, tuple):
            return [self.root + 12 * octave + s for s in spec]
        return [self.degree(spec + k, octave) for k in (0, 2, 4)]

    def part(self, instrument, notes, pan=0.0, echo=0.25):
        self.parts.append((instrument, pan, echo, notes))

    def melody(self, text, start=0.0, octave=0, vel=0.8):
        """Notes written as degree/beats: `5/2` is the fifth for two beats,
        `5'` an octave up, `5,` down, `5#` and `5b` a semitone up or down,
        `r/1` a beat's rest. Bars may be split with |."""
        notes = []
        at = start
        for tok in text.replace("|", " ").split():
            d, _, length = tok.partition("/")
            beats = float(length) if length else 1.0
            if d != "r":
                up = d.count("'") - d.count(",")
                sharp = d.count("#") - d.count("b")
                deg = int(d.strip("',#b"))
                notes.append((at, beats, self.degree(deg, octave + up) + sharp, vel))
            at += beats
        return notes, at - start


def chords_per_bar(p, progression):
    """One chord a bar (or (chord, bars) for longer), as a list of (beat, beats, chord)."""
    out = []
    at = 0.0
    for c in progression:
        spec, bars = (c if isinstance(c, list) else [c, 1])
        out.append((at, bars * p.bar, spec))
        at += bars * p.bar
    return out


def pad(p, chords, octave, vel):
    notes = []
    for at, beats, spec in chords:
        for m in p.chord(spec, octave):
            notes.append((at, beats, m, vel))
    return notes


def roots(p, chords, octave, vel, rhythm=None):
    """The chord roots, held, or struck at each (beat into the bar, beats,
    octaves up) of `rhythm`."""
    notes = []
    for at, beats, spec in chords:
        m = p.chord(spec, octave)[0]
        if rhythm is None:
            notes.append((at, beats, m, vel))
            continue
        for bar in range(int(beats // p.bar)):
            for k, (offset, length, up) in enumerate(rhythm):
                notes.append((at + bar * p.bar + offset, length, m + 12 * up, vel * (1 if k == 0 else 0.75)))
    return notes


def arpeggio(p, chords, octave, vel, pattern, step):
    """Chord tones in a repeating pattern: indices into the chord (3 is
    the root an octave up, and so on), one each `step` beats."""
    notes = []
    for at, beats, spec in chords:
        tones = p.chord(spec, octave)
        count = int(round(beats / step))
        for k in range(count):
            i = pattern[k % len(pattern)]
            m = tones[i % 3] + 12 * (i // 3)
            accent = 1.0 if (k * step) % p.bar == 0 else 0.8
            notes.append((at + k * step, step * 1.5, m, vel * accent))
    return notes


def drums(p, pattern, vel, bars=None, start_bar=0):
    """A drum hit at each beat offset in `pattern` (per bar), every bar."""
    notes = []
    for bar in range(start_bar, bars if bars is not None else p.bars):
        for offset, v in pattern:
            notes.append((bar * p.bar + offset, 0.25, None, vel * v))
    return notes


# --- Rendering ---------------------------------------------------------------------

def render(p, seed=1):
    """The piece as a stereo loop, two rows of samples."""
    beat = 60.0 / p.bpm
    total = _n(p.beats * beat)
    dry = np.zeros((2, total))
    wet = np.zeros((2, total))
    r = rng(seed)
    for pi, (inst, pan, echo, notes) in enumerate(p.parts):
        make = INSTRUMENTS[inst]
        left, right = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
        for ni, (at, beats, m, vel) in enumerate(notes):
            f = mtof(m) if m is not None else 0
            human = r.uniform(-0.008, 0.008) if m is not None else r.uniform(-0.004, 0.004)
            sig = make(f, beats * beat, vel * r.uniform(0.92, 1.05), seed * 1000 + pi * 97 + ni)
            start = _n(max(0.0, at * beat + human)) % total
            idx = (start + np.arange(len(sig))) % total  # wraps round: the piece loops
            for c, g in enumerate((left, right)):
                np.add.at(dry[c], idx, sig * g)
                np.add.at(wet[c], idx, sig * g * echo)
    irs = reverb_ir(2.4, 2.0, seed + 50, 7000, stereo=True)
    out = dry.copy()
    for c in range(2):
        ir = np.zeros(total)
        ir[: len(irs[c])] = irs[c][:total]
        out[c] += np.fft.irfft(np.fft.rfft(wet[c]) * np.fft.rfft(ir), total) * 1.4
    out = band(out[0], 30, 9500), band(out[1], 30, 9500)
    out = np.array(out)
    rms = np.sqrt((out ** 2).mean())
    out *= 0.16 / rms
    peak = np.abs(out).max()
    if peak > 0.97:
        out = np.tanh(out / 0.97 * 1.2) / np.tanh(1.2) * 0.97 if peak > 1.3 else out * 0.97 / peak
    return out
