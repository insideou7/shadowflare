"""The kinds of sound effect the open pack needs, each made from scratch
with tools/sound/synth.py. sfx.py says which kind each voice slot uses."""

import numpy as np

from synth import (RATE, adsr, band, bandsweep, drive, expdecay, fade, formants, harmonics, lowsweep,
                   mixdown, noise, peak, points, pulses, reverb, rng, saw, seconds, sine, smooth_noise, square,
                   sweep)


def _n(dur):
    return int(round(dur * RATE))


# --- Bodies and weapons -----------------------------------------------------

def step(seed, dur=0.13, bright=1.0):
    """A boot on earth: a soft thud and a scuff of grit."""
    n = _n(dur)
    thud = sine(points(n, [(0, 120), (dur, 60)]), n) * expdecay(n, 0.003, 0.06)
    grit = band(noise(n, seed), 400, 2500 * bright) * expdecay(n, 0.004, 0.08)
    return thud * 0.8 + grit * 0.5


def whoosh(seed, dur=0.2, lo=500, hi=2500, heavy=False):
    """A blade or limb cutting the air: noise through a band that rises
    and falls."""
    n = _n(dur)
    centre = lambda t: lo + (hi - lo) * np.sin(np.pi * min(t / dur, 1)) ** 1.5
    w = bandsweep(noise(n, seed), centre, q=2.5) * points(n, [(0, 0), (dur * 0.45, 1), (dur, 0)])
    if heavy:
        w += 0.4 * band(noise(n, seed + 1), 80, 400) * points(n, [(0, 0), (dur * 0.5, 1), (dur, 0)])
    return w


def twang(seed, dur=0.16, freq=180):
    """A bowstring let go: a plucked low string and a wooden knock."""
    n = _n(dur)
    amps = [expdecay(n, 0.001, 0.12 / k ** 0.5) / k for k in range(1, 16)]
    s = harmonics(points(n, [(0, freq * 1.05), (0.03, freq)]), n, amps)
    knock = band(noise(n, seed), 600, 3000) * expdecay(n, 0.001, 0.02)
    return s + knock * 0.6


def impact(seed, dur=0.3, weight=1.0, crunch=0.5, metal=0.0):
    """A blow landing on a body: a low punch, a slap and a crunch."""
    n = _n(dur)
    low = 90 / weight ** 0.3
    punch = sine(points(n, [(0, low * 2.2), (0.06, low)]), n) * expdecay(n, 0.002, 0.15 * weight)
    slap = band(noise(n, seed), 300, 4000) * expdecay(n, 0.001, 0.05)
    cr = band(noise(n, seed + 1), 1200, 6000) * expdecay(n, 0.003, 0.09) * (smooth_noise(n, 300, seed) > 0.2)
    out = punch + slap * 0.8 + cr * crunch
    if metal:
        out += metal * clang(seed + 3, dur, 900 + 200 * (seed % 5)) * 0.5
    return drive(out, 1.6)


def clang(seed, dur=0.4, freq=900):
    """Struck metal: partials out of tune with each other, ringing down."""
    n = _n(dur)
    ratios = [1, 1.47, 2.09, 2.56, 3.39, 4.17, 5.43]
    r = rng(seed)
    out = np.zeros(n)
    for k, ratio in enumerate(ratios):
        out += sine(freq * ratio * r.uniform(0.99, 1.01), n) * expdecay(n, 0.0005, dur * 0.9 / (1 + k * 0.4)) / (1 + k * 0.5)
    hit = band(noise(n, seed), 2000, 8000) * expdecay(n, 0.0005, 0.01)
    return out + hit * 0.5


def knock(seed, dur=0.15, freq=300, wood=1.0):
    """Wood struck: a short hollow note and a click."""
    n = _n(dur)
    body = (sine(freq, n) + 0.5 * sine(freq * 2.3, n) * wood) * expdecay(n, 0.0005, dur * 0.5)
    click = band(noise(n, seed), 1000, 5000) * expdecay(n, 0.0005, 0.008)
    return body + click * 0.7


def arrow_hit(seed, dur=0.21):
    """An arrow striking: the thunk of the head and the shaft shivering."""
    n = _n(dur)
    thunk = knock(seed, dur, 220)
    shiver = sine(410 * (1 + 0.03 * np.sin(2 * np.pi * 38 * seconds(dur)[:n])), n) * expdecay(n, 0.005, dur * 0.8) * 0.25
    return thunk + shiver + impact(seed + 1, dur, 0.6, 0.2) * 0.4


def voice(seed, dur, f0, vowels, female=False, rough=0.0, breath=0.2, vib=0.0, shift=None):
    """A human or creature voice: a pulse train with a pitch contour,
    shaped by vowel formants. f0 and vowels are (time, value) lists over
    0-1 of the length."""
    n = _n(dur)
    pitch = points(n, [(t * dur, f) for t, f in f0])
    if vib:
        pitch = pitch * (1 + vib * np.sin(2 * np.pi * 5.5 * np.arange(n) / RATE))
    src = pulses(pitch, n, seed, jitter=0.02 + rough * 0.08, width=0.4)
    if rough:
        sub = 0.5 + 0.5 * np.sign(np.sin(np.pi * np.cumsum(pitch) / RATE))  # a growl an octave down
        src = src * (1 - rough * 0.6 + rough * 0.6 * sub)
        src += rough * 0.1 * band(noise(n, seed + 1), 200, 3000) * np.abs(smooth_noise(n, 60, seed))
    src += breath * 0.08 * band(noise(n, seed + 2), 300, 6000)
    vt = [(t * dur, v) for t, v in vowels]

    def vowel_at(t):
        for (t0, v0), (t1, v1) in zip(vt, vt[1:]):
            if t0 <= t <= t1:
                return (v0, v1, (t - t0) / max(t1 - t0, 1e-6))
        return vt[-1][1] if t > vt[-1][0] else vt[0][1]

    sh = shift if shift is not None else (1.18 if female else 1.0)
    out = formants(src, vowel_at, shift=sh, breath=0.02)
    return out * adsr(n, 0.015, 0.05, 0.85, dur * 0.35)


def shout(seed, female=False, dur=0.23, effort=1.0):
    """The hero's battle cry as a blow falls."""
    f = 300 if female else 155
    v = voice(seed, dur, [(0, f * 1.05), (0.3, f * 1.25 * effort), (1, f * 0.85)], [(0, "a"), (1, "ah")],
              female, rough=0.15, breath=0.15)
    return drive(v, 1.5)


def defeat_cry(seed, female=False, dur=1.6):
    """The hero falling: a long cry that sinks away."""
    f = 330 if female else 170
    v = voice(seed, dur, [(0, f * 1.3), (0.15, f * 1.45), (0.6, f * 1.1), (1, f * 0.6)],
              [(0, "a"), (0.5, "ah"), (1, "o")], female, rough=0.1, breath=0.25, vib=0.02)
    return reverb(v, 0.2, 0.8, seed)


def grunt(seed, female=False, dur=0.25):
    f = 260 if female else 130
    return drive(voice(seed, dur, [(0, f * 1.2), (1, f * 0.8)], [(0, "uh"), (1, "u")], female, rough=0.3, breath=0.3), 2)


# --- Creatures ---------------------------------------------------------------

# Each creature's voice: base pitch, vowels, roughness, breathiness and
# formant shift (bigger beasts have lower formants).
CREATURES = {
    "goblin": dict(f0=260, vowels=["e", "a"], rough=0.5, breath=0.3, shift=1.35),
    "shaman": dict(f0=230, vowels=["i", "a"], rough=0.35, breath=0.35, shift=1.3),
    "ghoul": dict(f0=95, vowels=["o", "uh"], rough=0.7, breath=0.6, shift=0.9),
    "demon": dict(f0=70, vowels=["a", "o"], rough=0.9, breath=0.3, shift=0.7),
    "ogre": dict(f0=85, vowels=["uh", "o"], rough=0.6, breath=0.3, shift=0.75),
    "boar": dict(f0=380, vowels=["i", "e"], rough=0.8, breath=0.4, shift=1.1),
    "dragon": dict(f0=55, vowels=["a", "o"], rough=1.0, breath=0.5, shift=0.6),
    "barbarian": dict(f0=140, vowels=["a", "ah"], rough=0.3, breath=0.2, shift=1.0),
    "man": dict(f0=150, vowels=["a", "o"], rough=0.2, breath=0.2, shift=1.0),
    "lizard": dict(f0=200, vowels=["e", "i"], rough=0.6, breath=0.8, shift=1.2),
    "vampire": dict(f0=420, vowels=["i", "e"], rough=0.4, breath=0.7, shift=1.25),
    "raptor": dict(f0=520, vowels=["e", "i"], rough=0.7, breath=0.4, shift=1.4),
    "beast": dict(f0=65, vowels=["o", "a"], rough=0.9, breath=0.5, shift=0.65),
    "wolf": dict(f0=380, vowels=["u", "o"], rough=0.1, breath=0.2, shift=1.1),
}


def creature(seed, kind, dur, mood="cry"):
    """A creature's sound. mood: cry (pain or death), roar (attacking),
    grunt (effort), shriek (high and long), growl (low, steady)."""
    c = CREATURES[kind]
    f = c["f0"]
    v0, v1 = c["vowels"]
    r = rng(seed)
    f *= r.uniform(0.93, 1.07)
    contour = {
        "cry": [(0, f * 1.2), (0.12, f * 1.5), (0.5, f * 1.1), (1, f * 0.55)],
        "roar": [(0, f * 0.8), (0.2, f * 1.15), (0.7, f * 1.0), (1, f * 0.7)],
        "grunt": [(0, f * 1.1), (1, f * 0.8)],
        "shriek": [(0, f * 1.6), (0.2, f * 2.0), (0.8, f * 1.7), (1, f * 1.2)],
        "growl": [(0, f * 0.7), (0.5, f * 0.75), (1, f * 0.65)],
    }[mood]
    vowels = [(0, v0), (0.6, v1), (1, v1 if mood != "cry" else "o")]
    out = voice(seed, dur, contour, vowels, rough=c["rough"], breath=c["breath"], shift=c["shift"])
    if mood in ("roar", "growl") and c["rough"] > 0.6:
        out = out * (0.75 + 0.25 * np.sin(2 * np.pi * 28 * np.arange(len(out)) / RATE))
    return reverb(drive(out, 2.2), 0.12, 0.5, seed)


def buzz(seed, dur, freq=190):
    """Wings beating: a buzzing tone that wavers."""
    n = _n(dur)
    f = freq * (1 + 0.06 * smooth_noise(n, 8, seed))
    b = saw(f, n, 30) * (0.7 + 0.3 * np.sin(2 * np.pi * 21 * np.arange(n) / RATE))
    return band(b, 150, 4000) * adsr(n, 0.03, 0.05, 0.8, dur * 0.3)


def chitter(seed, dur, rate=28, freq=3200):
    """Clicking mandibles or legs: a fast run of tiny clicks."""
    n = _n(dur)
    out = np.zeros(n)
    r = rng(seed)
    t = 0.0
    while t < dur - 0.01:
        c = band(noise(_n(0.01), int(r.integers(1 << 30))), freq * 0.6, freq * 1.6) * expdecay(_n(0.01), 0.0003, 0.006)
        out[_n(t):_n(t) + len(c)] += c[: n - _n(t)] * r.uniform(0.4, 1)
        t += r.uniform(0.6, 1.4) / rate
    return out * adsr(n, 0.01, 0.02, 1, dur * 0.2)


def rattle(seed, dur, freq=1400):
    """Bones knocking together."""
    n = _n(dur)
    out = np.zeros(n)
    r = rng(seed)
    t = 0.0
    while t < dur - 0.04:
        k = knock(int(r.integers(1 << 30)), 0.04, freq * r.uniform(0.7, 1.4), 0.6)
        place_at = _n(t)
        out[place_at:place_at + len(k)] += k[: n - place_at] * r.uniform(0.3, 1)
        t += r.uniform(0.02, 0.07)
    return out * points(n, [(0, 1), (dur, 0.2)])


def rocks(seed, dur, size=1.0):
    """Stone breaking and tumbling: a deep crack and falling rubble."""
    n = _n(dur)
    crack = impact(seed, min(dur, 0.4), 2.0 * size, 0.9)
    rubble = np.zeros(n)
    r = rng(seed + 9)
    t = 0.05
    while t < dur * 0.85:
        k = knock(int(r.integers(1 << 30)), 0.06, r.uniform(120, 500) / size, 0.3)
        rubble[_n(t):_n(t) + len(k)] += k[: n - _n(t)] * r.uniform(0.2, 0.7) * (1 - t / dur)
        t += r.uniform(0.01, 0.06)
    rumble = band(noise(n, seed + 3), 30, 250) * points(n, [(0, 0), (0.05, 1), (dur, 0)])
    return mixdown(dur, [(0, crack), (0, rubble * 0.8), (0, rumble * 0.6)])


def hiss(seed, dur, bright=5000):
    n = _n(dur)
    return band(noise(n, seed), bright * 0.5, bright * 1.6) * adsr(n, 0.04, 0.05, 0.8, dur * 0.5)


def stomp(seed, dur=0.33, weight=2.0):
    """A heavy foot coming down: a deep boom and shaken grit."""
    n = _n(dur)
    boom = sine(points(n, [(0, 80), (0.15, 40)]), n) * expdecay(n, 0.003, dur * 0.8)
    grit = band(noise(n, seed), 150, 1500) * expdecay(n, 0.002, 0.12)
    return drive(boom * weight + grit * 0.6, 1.5)


def flap(seed, dur, rate=4.0):
    """Big wings: whooshes beating `rate` times a second."""
    n = _n(dur)
    ph = (np.arange(n) / RATE) * rate
    env = np.maximum(0, np.sin(2 * np.pi * ph)) ** 3
    return band(noise(n, seed), 100, 1500) * env * adsr(n, 0.02, 0.05, 1, dur * 0.2)


# --- Magic -------------------------------------------------------------------

def crackle(seed, dur, density=60, lo=1500, hi=7000):
    n = _n(dur)
    r = rng(seed)
    out = np.zeros(n)
    for _ in range(int(density * dur)):
        at = int(r.integers(0, max(1, n - 200)))
        length = int(r.integers(20, 200))
        out[at:at + length] += r.uniform(-1, 1) * expdecay(length, 0.0001, length / RATE * 0.5)
    return band(out, lo, hi)


def fire(seed, dur=0.6, size=1.0):
    """A fireball leaving the hand: a roaring whoosh and crackle."""
    n = _n(dur)
    roar = lowsweep(noise(n, seed), lambda t: 300 + 2500 * size * np.exp(-3 * t / dur))
    roar = roar * points(n, [(0, 0), (0.04, 1), (dur * 0.5, 0.6), (dur, 0)])
    return roar + 0.5 * crackle(seed + 1, dur, 80) * points(n, [(0, 1), (dur, 0)])


def zap(seed, dur=1.0, freq=180):
    """Electric discharge: a crack, then a buzz that wavers and crackles."""
    n = _n(dur)
    crack = band(noise(n, seed), 800, 9000) * expdecay(n, 0.0005, 0.08)
    f = freq * (1 + 0.3 * smooth_noise(n, 25, seed))
    hum = square(f, n, 25) * (np.abs(smooth_noise(n, 40, seed + 1)) > 0.3) * expdecay(n, 0.01, dur * 0.8)
    return crack + 0.35 * band(hum, 100, 6000) + 0.6 * crackle(seed + 2, dur, 120) * expdecay(n, 0.001, dur)


def thunder(seed, dur=1.5):
    n = _n(dur)
    crack = band(noise(n, seed), 500, 8000) * expdecay(n, 0.001, 0.12)
    rumble = band(noise(n, seed + 1), 30, 300) * (0.6 + 0.4 * np.abs(smooth_noise(n, 8, seed))) * expdecay(n, 0.05, dur)
    return reverb(crack + rumble * 1.5, 0.3, 1.2, seed)


def magic_hit(seed, dur=1.0):
    """A spell striking: a bright burst and a falling boom."""
    n = _n(dur)
    boom = sine(points(n, [(0, 180), (0.3, 50)]), n) * expdecay(n, 0.002, dur * 0.6)
    burst = lowsweep(noise(n, seed), lambda t: 200 + 6000 * np.exp(-6 * t / dur)) * expdecay(n, 0.002, dur * 0.7)
    shimmer = sum(sine(f, n) * expdecay(n, 0.001, dur * 0.5) for f in (1320, 1760, 2490)) * 0.08
    return drive(boom + burst + shimmer, 1.4)


def explosion(seed, dur=1.0, size=1.0):
    n = _n(dur)
    body = lowsweep(noise(n, seed), lambda t: 150 + 5000 * size * np.exp(-5 * t / dur)) * expdecay(n, 0.003, dur * 0.85)
    sub = sine(points(n, [(0, 110), (0.25, 35)]), n) * expdecay(n, 0.002, dur * 0.6)
    debris = crackle(seed + 4, dur, 50, 800, 5000) * points(n, [(0, 0), (0.1, 1), (dur, 0)])
    return drive(body * 1.2 + sub * 1.2 * size + debris * 0.5, 1.8)


def ice(seed, dur=0.6, freq=2000):
    """Ice forming or shattering: glassy partials and a sharp crack."""
    n = _n(dur)
    r = rng(seed)
    out = np.zeros(n)
    for k in range(9):
        f = freq * r.uniform(0.6, 2.4)
        at = r.uniform(0, dur * 0.3)
        tone = sine(f, n - _n(at)) * expdecay(n - _n(at), 0.001, dur * r.uniform(0.2, 0.6))
        out[_n(at):] += tone * r.uniform(0.2, 0.5)
    crack = band(noise(n, seed), 2500, 9000) * expdecay(n, 0.0005, 0.05)
    return reverb(out + crack, 0.25, 0.6, seed)


def earth(seed, dur=0.4):
    """The ground bursting: a thump and flung grit."""
    return rocks(seed, dur, 0.8) + 0.4 * explosion(seed + 2, dur, 0.4)


def shimmer(seed, dur=0.5, freq=880, rise=True, chord=(1, 1.25, 1.5, 2)):
    """Bells and sparkle running up (or down) a chord."""
    n = _n(dur)
    out = np.zeros(n)
    steps = list(chord) if rise else list(chord)[::-1]
    for k, ratio in enumerate(steps):
        at = dur * 0.6 * k / len(steps)
        m = n - _n(at)
        tone = bell(seed + k, m / RATE, freq * ratio)
        out[_n(at):] += tone[:m] * 0.5
    sparkle = band(noise(n, seed), 4000, 9000) * np.abs(smooth_noise(n, 30, seed)) * expdecay(n, 0.05, dur) * 0.15
    return reverb(out + sparkle, 0.35, 1.0, seed)


def bell(seed, dur, freq):
    n = _n(dur)
    out = np.zeros(n)
    for ratio, amp, d in ((1, 1, 1.0), (2.0, 0.5, 0.6), (2.76, 0.35, 0.4), (5.4, 0.2, 0.2), (8.93, 0.1, 0.1)):
        if freq * ratio < RATE * 0.45:
            out += amp * sine(freq * ratio, n) * expdecay(n, 0.001, dur * d)
    return out


def drone(seed, dur, freq, wobble=4.0):
    """A humming, throbbing tone."""
    n = _n(dur)
    tone = saw(freq * (1 + 0.01 * smooth_noise(n, 3, seed)), n, 20, 1.5)
    tone *= 0.6 + 0.4 * np.sin(2 * np.pi * wobble * np.arange(n) / RATE)
    return band(tone, 60, 2500) * adsr(n, 0.05, 0.05, 0.9, dur * 0.4)


def suck(seed, dur=0.5):
    """Life drawn out: a reversed swell into a soft pop."""
    n = _n(dur)
    s = bandsweep(noise(n, seed), lambda t: 300 + 2000 * (t / dur) ** 2, q=3) * points(n, [(0, 0), (dur * 0.9, 1), (dur, 0)])
    return s + 0.4 * sine(points(n, [(0, 200), (dur, 600)]), n) * points(n, [(0, 0), (dur * 0.9, 1), (dur, 0)])


def warble(seed, dur, freq=440):
    """A portal: tones bending round each other with a shimmer."""
    n = _n(dur)
    t = np.arange(n) / RATE
    out = sum(sine(freq * r * (1 + 0.02 * np.sin(2 * np.pi * (3 + k) * t)), n) / (k + 1) for k, r in enumerate((1, 1.5, 2.01, 3)))
    return reverb(out * adsr(n, dur * 0.2, 0.1, 0.8, dur * 0.4) + 0.1 * band(noise(n, seed), 3000, 9000) * adsr(n, 0.1, 0.1, 0.5, 0.2), 0.4, 1.0, seed)


# --- Things ------------------------------------------------------------------

def rustle(seed, dur=0.25, bright=3000):
    """Cloth and leather handled."""
    n = _n(dur)
    env = np.abs(smooth_noise(n, 40, seed)) ** 2 * adsr(n, 0.01, 0.05, 0.8, dur * 0.4)
    return band(noise(n, seed + 1), 600, bright) * env


def coins(seed, dur=0.3):
    n = _n(dur)
    out = np.zeros(n)
    r = rng(seed)
    for k in range(6):
        at = r.uniform(0, dur * 0.5)
        c = bell(int(r.integers(1 << 30)), dur - at, r.uniform(3000, 5200))
        out[_n(at):_n(at) + len(c)] += c[: n - _n(at)] * r.uniform(0.2, 0.6)
    return out


def chime(seed, dur, notes, gap=0.08, freq=880):
    """Bell notes one after another (semitones above freq)."""
    n = _n(dur)
    out = np.zeros(n)
    for k, semis in enumerate(notes):
        at = k * gap
        if at >= dur:
            break
        b = bell(seed + k, dur - at, freq * 2 ** (semis / 12))
        out[_n(at):_n(at) + len(b)] += b[: n - _n(at)]
    return out


def blip(seed, dur, f1, f2=None, shape="sine"):
    """A short interface tone, gliding from f1 to f2."""
    n = _n(dur)
    f = points(n, [(0, f1), (dur, f2 or f1)])
    tone = sine(f, n) if shape == "sine" else harmonics(f, n, [1, 0.3, 0.15, 0.08])
    return tone * expdecay(n, 0.002, dur * 0.7)


def click(seed, dur=0.05, freq=2500):
    n = _n(dur)
    return band(noise(n, seed), freq * 0.5, freq * 2) * expdecay(n, 0.0003, 0.01) + 0.3 * sine(freq * 0.4, n) * expdecay(n, 0.0005, 0.02)


def creak(seed, dur=0.8, freq=180):
    """A hinge or rope straining: a scratchy tone that sticks and slips."""
    n = _n(dur)
    f = freq * (1 + 0.4 * smooth_noise(n, 6, seed))
    stick = pulses(f * 0.25, n, seed, 0.2, 0.2)
    tone = formants(stick, lambda t: "e", shift=1.6)
    return tone * adsr(n, 0.05, 0.05, 0.9, dur * 0.3)


def grind(seed, dur=1.0, freq=60):
    """Stone or iron dragged: a low scraping rumble."""
    n = _n(dur)
    scrape = band(noise(n, seed), 80, 1200) * (0.6 + 0.4 * np.abs(smooth_noise(n, 15, seed)))
    tone = saw(freq * (1 + 0.05 * smooth_noise(n, 3, seed + 1)), n, 20) * 0.2
    return (scrape + tone) * adsr(n, 0.08, 0.1, 0.9, dur * 0.3)


def gears(seed, dur=1.0, rate=10):
    """Machinery turning: a ratchet over a grinding hum."""
    n = _n(dur)
    out = grind(seed, dur, 50) * 0.6
    t = 0.02
    while t < dur - 0.05:
        k = clang(seed + int(t * 1000), 0.05, 1200) * 0.3 + knock(seed + int(t * 997), 0.05, 400) * 0.5
        out[_n(t):_n(t) + len(k)] += k[: n - _n(t)]
        t += 1 / rate
    return out


def splash(seed, dur=0.6):
    n = _n(dur)
    s = lowsweep(noise(n, seed), lambda t: 6000 * np.exp(-4 * t / dur) + 300) * expdecay(n, 0.005, dur * 0.7)
    bubbles = gulp(seed + 1, dur, 6) * 0.3
    return s + bubbles


def gulp(seed, dur=0.3, count=2):
    """Drinking: water bubbles that rise in pitch."""
    n = _n(dur)
    out = np.zeros(n)
    r = rng(seed)
    for k in range(count):
        at = dur * k / count
        m = _n(min(0.08, dur - at))
        if m <= 0:
            break
        b = sine(points(m, [(0, r.uniform(300, 500)), (m / RATE, r.uniform(800, 1300))]), m) * expdecay(m, 0.003, 0.06)
        out[_n(at):_n(at) + m] += b
    return out + 0.2 * rustle(seed, dur, 2000)


def latch(seed, dur=0.3):
    """A lock or catch snapping: two metal clicks."""
    return mixdown(dur, [(0, clang(seed, 0.12, 2200) * 0.5 + click(seed, 0.05, 3000)), (0.07, knock(seed + 1, 0.1, 500) * 0.8)])


def door(seed, dur=1.0, closing=False):
    """A wooden door swinging on its hinges and knocking home."""
    parts = [(0, creak(seed, dur * 0.8, 160 if closing else 200) * 0.6)]
    parts.append((dur * 0.7 if closing else dur * 0.05, knock(seed + 1, 0.25, 110, 1.5) * (1.2 if closing else 0.5)))
    return mixdown(dur, parts)


def lever(seed, dur=0.8):
    """A lever thrown: a ratchet, then a clunk."""
    n = _n(dur)
    out = np.zeros(n)
    for k in range(4):
        c = click(seed + k, 0.04, 1800) + knock(seed + k, 0.04, 600, 0.3) * 0.5
        place = _n(k * dur * 0.12)
        out[place:place + len(c)] += c[: n - place]
    clunk = clang(seed + 7, dur * 0.5, 320) * 0.4 + knock(seed + 8, dur * 0.4, 140) * 0.9
    out[_n(dur * 0.5):_n(dur * 0.5) + len(clunk)] += clunk[: n - _n(dur * 0.5)]
    return out


def ring(seed, dur, freq=880):
    """A small metal ring or jewel set down."""
    return bell(seed, dur, freq) * 0.6 + bell(seed + 1, dur, freq * 1.502) * 0.3 + click(seed, 0.03, 4000) * 0.4


def beep(seed, dur=0.26, freq=1100):
    n = _n(dur)
    return square(freq, n, 8) * adsr(n, 0.005, 0.02, 0.8, 0.03) * 0.5


def wind(seed, dur=1.0, pitch=600):
    n = _n(dur)
    return bandsweep(noise(n, seed), lambda t: pitch * (1 + 0.5 * np.sin(2 * np.pi * t / dur)), q=6) * adsr(n, dur * 0.3, 0.1, 0.8, dur * 0.4)


def bark(seed, dur=0.24, big=True):
    """A dog's bark."""
    f = 330 if big else 520
    v = voice(seed, dur, [(0, f), (0.2, f * 1.35), (1, f * 0.8)], [(0, "u"), (0.25, "a"), (1, "uh")],
              rough=0.5, breath=0.3, shift=1.0 if big else 1.25)
    return drive(v, 2.5)


def pant(seed, dur=1.1):
    """A dog panting."""
    n = _n(dur)
    env = np.maximum(0, np.sin(2 * np.pi * 3.5 * np.arange(n) / RATE)) ** 2
    return formants(band(noise(n, seed), 300, 5000), lambda t: "ah", 1.1) * env * adsr(n, 0.05, 0.05, 1, 0.2)


# --- Sounds of different lengths add up --------------------------------------

class Sound(np.ndarray):
    """A mono sound. Adding two sounds of different lengths pads the
    shorter one with silence, so kinds can be layered freely."""

    __array_priority__ = 10

    def _pad(self, other):
        if isinstance(other, np.ndarray) and other.ndim == 1 and len(other) != len(self):
            n = max(len(self), len(other))
            a = np.zeros(n)
            b = np.zeros(n)
            a[: len(self)] = self
            b[: len(other)] = other
            return a.view(Sound), b
        return self.view(np.ndarray), other

    def __add__(self, other):
        a, b = self._pad(other)
        return np.add(np.asarray(a), b).view(Sound)

    __radd__ = __add__


def _sounding(fn):
    def wrapped(*args, **kwargs):
        return np.asarray(fn(*args, **kwargs), dtype=float).view(Sound)
    wrapped.__name__ = fn.__name__
    wrapped.__doc__ = fn.__doc__
    return wrapped


for _name, _fn in list(globals().items()):
    if callable(_fn) and not _name.startswith("_") and getattr(_fn, "__module__", None) == __name__ and _name != "Sound":
        globals()[_name] = _sounding(_fn)
