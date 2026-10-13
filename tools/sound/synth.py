"""A small synthesizer for the open pack's sounds and music.

Everything is made from oscillators, noise and filters with numpy, so the
sounds are our own and the same on every run (every random source takes a
seed). Nothing is sampled from any recording.
"""

import numpy as np

RATE = 22050


def seconds(n):
    return np.arange(int(round(n * RATE))) / RATE


def rng(seed):
    return np.random.default_rng(seed)


# --- Envelopes -------------------------------------------------------------

def points(n, pts):
    """A piecewise-linear curve through (time, value) points, n samples long."""
    t = np.arange(n) / RATE
    ts, vs = zip(*pts)
    return np.interp(t, ts, vs)


def expdecay(n, attack, decay, hold=0.0):
    """Rises over `attack` seconds, holds, then falls by 60 dB a `decay` seconds."""
    t = np.arange(n) / RATE
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    d = np.where(t < attack + hold, 1.0, 10 ** (-3 * (t - attack - hold) / max(decay, 1e-4)))
    return a * d


def adsr(n, a, d, s, r):
    """Attack, decay, sustain level and release, the release at the end."""
    t = np.arange(n) / RATE
    total = n / RATE
    rel = max(total - r, a + d)
    e = np.interp(t, [0, a, a + d, rel, total], [0, 1, s, s, 0])
    return e


def fade(sig, inn=0.002, out=0.01):
    n = len(sig)
    e = np.ones(n)
    i, o = int(inn * RATE), int(out * RATE)
    if i:
        e[:i] = np.linspace(0, 1, i)
    if o:
        e[-o:] *= np.linspace(1, 0, o)
    return sig * e


# --- Sources ---------------------------------------------------------------

def phase(freq, n):
    f = np.broadcast_to(np.asarray(freq, dtype=float), (n,))
    return 2 * np.pi * np.cumsum(f) / RATE


def sine(freq, n):
    return np.sin(phase(freq, n))


def harmonics(freq, n, amps, phase0=None):
    """A tone from harmonics: amps[k] for harmonic k+1 (a scalar or a curve),
    leaving out any above the Nyquist limit."""
    ph = phase(freq, n)
    f = np.broadcast_to(np.asarray(freq, dtype=float), (n,))
    out = np.zeros(n)
    for k, a in enumerate(amps, start=1):
        mask = (f * k) < RATE * 0.45
        if not mask.any():
            break
        p = 0 if phase0 is None else phase0[k - 1]
        out += np.asarray(a) * np.sin(k * ph + p) * mask
    return out


def saw(freq, n, count=40, tilt=1.0):
    return harmonics(freq, n, [1 / k ** tilt for k in range(1, count + 1)])


def square(freq, n, count=30):
    return harmonics(freq, n, [(1 / k if k % 2 else 0) for k in range(1, count + 1)])


def noise(n, seed):
    return rng(seed).standard_normal(n)


def pulses(freq, n, seed=0, jitter=0.0, width=0.4):
    """A glottal pulse train (Rosenberg's shape, as the air it lets through
    sounds: its slope), the source of voices and growls."""
    f = np.asarray(freq, dtype=float) * (1 + jitter * smooth_noise(n, 30, seed))
    ph = phase(f, n) / (2 * np.pi)
    frac = ph % 1.0
    rise = np.clip(width, 0.05, 0.9)
    fall = rise * 0.4
    flow = np.where(frac < rise, 0.5 * (1 - np.cos(np.pi * frac / rise)),
                    np.where(frac < rise + fall, np.cos(0.5 * np.pi * (frac - rise) / fall), 0.0))
    slope = np.diff(flow, prepend=flow[0]) * RATE / np.maximum(np.asarray(f, dtype=float), 1) / 8
    return slope


def smooth_noise(n, rate_hz, seed):
    """Noise that wanders `rate_hz` times a second, -1 to 1."""
    count = max(2, int(n / RATE * rate_hz) + 2)
    r = rng(seed).uniform(-1, 1, count)
    return np.interp(np.arange(n), np.linspace(0, n, count), r)


# --- Filters ---------------------------------------------------------------

def _gain_curve(freqs, lo, hi, slope):
    g = np.ones_like(freqs)
    if lo:
        g *= 1 / (1 + (lo / np.maximum(freqs, 1)) ** slope)
    if hi:
        g *= 1 / (1 + (freqs / hi) ** slope)
    return g


def band(sig, lo=None, hi=None, slope=4):
    """A fixed filter: high-pass at lo and low-pass at hi, in Hz."""
    spec = np.fft.rfft(sig)
    freqs = np.fft.rfftfreq(len(sig), 1 / RATE)
    return np.fft.irfft(spec * _gain_curve(freqs, lo, hi, slope), len(sig))


def peak(sig, centre, width, gain=1.0):
    """Adds a resonance at `centre` Hz, `width` Hz wide."""
    spec = np.fft.rfft(sig)
    freqs = np.fft.rfftfreq(len(sig), 1 / RATE)
    g = 1 + gain * np.exp(-0.5 * ((freqs - centre) / width) ** 2)
    return np.fft.irfft(spec * g, len(sig))


def sweep(sig, gain_at, frame=512, hop=128):
    """A filter that changes over time: gain_at(freqs, t) gives the gain of
    each frequency at time t (seconds). Short frames, overlap-added."""
    n = len(sig)
    pad = np.concatenate([np.zeros(frame), sig, np.zeros(frame)])
    win = np.hanning(frame)
    out = np.zeros(len(pad))
    norm = np.zeros(len(pad))
    freqs = np.fft.rfftfreq(frame, 1 / RATE)
    for start in range(0, len(pad) - frame, hop):
        seg = pad[start:start + frame] * win
        t = (start - frame + frame / 2) / RATE
        spec = np.fft.rfft(seg) * gain_at(freqs, max(t, 0))
        out[start:start + frame] += np.fft.irfft(spec, frame) * win
        norm[start:start + frame] += win ** 2
    out /= np.maximum(norm, 1e-6)
    return out[frame:frame + n]


def bandsweep(sig, centre, q=2.0, floor=0.0):
    """A band-pass whose centre follows centre(t), in Hz; q is the centre
    over the width."""
    def gain(freqs, t):
        c = centre(t)
        return floor + np.exp(-0.5 * ((freqs - c) / max(c / q, 10)) ** 2)
    return sweep(sig, gain)


def lowsweep(sig, cutoff, slope=3):
    """A low-pass whose cutoff follows cutoff(t) in Hz."""
    def gain(freqs, t):
        return 1 / (1 + (freqs / max(cutoff(t), 20)) ** slope)
    return sweep(sig, gain)


# Vowel formants (Hz, width, level), for voices, cries and choirs.
VOWELS = {
    "a": [(800, 80, 1.0), (1150, 90, 0.5), (2900, 120, 0.25), (3900, 130, 0.1)],
    "e": [(400, 60, 1.0), (1700, 80, 0.4), (2600, 100, 0.3), (3500, 120, 0.1)],
    "i": [(280, 50, 1.0), (2250, 90, 0.3), (2900, 100, 0.25), (3500, 120, 0.1)],
    "o": [(450, 70, 1.0), (800, 80, 0.6), (2830, 100, 0.1), (3500, 120, 0.05)],
    "u": [(325, 50, 1.0), (700, 60, 0.3), (2530, 100, 0.05), (3500, 120, 0.03)],
    "uh": [(600, 80, 1.0), (1040, 80, 0.6), (2250, 100, 0.2), (3000, 120, 0.1)],
    "ah": [(700, 130, 1.0), (1220, 140, 0.5), (2600, 160, 0.2), (3500, 200, 0.05)],
}


def formants(sig, vowel_at, shift=1.0, breath=0.0):
    """Shapes a source into a vowel. vowel_at(t) gives a vowel name, or a
    pair of names and how far from the first to the second."""
    def gain(freqs, t):
        v = vowel_at(t)
        if isinstance(v, str):
            fs = VOWELS[v]
        else:
            a, b, k = v
            fs = [tuple(x * (1 - k) + y * k for x, y in zip(fa, fb)) for fa, fb in zip(VOWELS[a], VOWELS[b])]
        g = np.full_like(freqs, breath)
        for f, w, l in fs:
            width = max(w * 2.2, f * 0.14) * shift
            g += l * np.exp(-0.5 * ((freqs - f * shift) / width) ** 2)
        return g
    return sweep(sig, gain)


# --- Effects ---------------------------------------------------------------

def reverb_ir(dur, decay, seed, bright=6000, stereo=False):
    n = int(dur * RATE)
    t = np.arange(n) / RATE
    chans = []
    for c in range(2 if stereo else 1):
        r = noise(n, seed + c)
        lo = band(r, hi=bright * 0.3) * 10 ** (-3 * t / decay)
        hi = band(r, lo=bright * 0.3, hi=bright) * 10 ** (-3 * t / (decay * 0.45))
        ir = lo + hi
        ir[: int(0.008 * RATE)] *= np.linspace(0, 1, int(0.008 * RATE))
        chans.append(ir / np.sqrt((ir ** 2).sum()))
    return chans


def reverb(sig, mix=0.25, decay=1.2, seed=7, bright=6000, tail=True):
    """Adds a room: mix is how loud the echo is against the dry sound."""
    ir = reverb_ir(min(decay * 1.2, 3.0), decay, seed, bright)[0]
    n = len(sig) + (len(ir) if tail else 0)
    wet = np.fft.irfft(np.fft.rfft(sig, n + len(ir)) * np.fft.rfft(ir, n + len(ir)), n + len(ir))[:n]
    dry = np.concatenate([sig, np.zeros(n - len(sig))])
    return dry + mix * wet


def drive(sig, amount=2.0):
    return np.tanh(sig * amount) / np.tanh(amount)


def normalize(sig, level=0.9):
    m = np.max(np.abs(sig))
    return sig * (level / m) if m > 0 else sig


def trim(sig, floor=0.0015):
    """Drops the silent end of a sound (below `floor` of its peak)."""
    m = np.max(np.abs(sig))
    if m == 0:
        return sig
    loud = np.nonzero(np.abs(sig) > floor * m)[0]
    end = min(len(sig), loud[-1] + int(0.01 * RATE)) if len(loud) else len(sig)
    return fade(sig[:end], 0.0, 0.008)


def place(out, sig, at):
    """Adds sig into out, starting `at` seconds in."""
    i = int(at * RATE)
    if i >= len(out):
        return out
    j = min(len(out), i + len(sig))
    out[i:j] += sig[: j - i]
    return out


def mixdown(dur, parts):
    """parts: (start seconds, signal) pairs mixed into one buffer."""
    out = np.zeros(int(dur * RATE))
    for at, s in parts:
        place(out, s, at)
    return out


def write_wav(path, sig, rate=RATE):
    import wave
    data = np.asarray(sig)
    chans = 1 if data.ndim == 1 else data.shape[0]
    frames = data if data.ndim == 1 else data.T.reshape(-1)
    pcm = (np.clip(frames, -1, 1) * 32767).astype("<i2").tobytes()
    with wave.open(str(path), "wb") as w:
        w.setnchannels(chans)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
