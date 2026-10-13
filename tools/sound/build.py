"""Makes the open pack's sounds and music (docs/sound.md, docs/content.md).

    python3 tools/sound/build.py               # everything, into content/
    python3 tools/sound/build.py --sfx 6 29    # only those effects
    python3 tools/sound/build.py --music town  # only that piece
    python3 tools/sound/build.py --wav out/    # also keep WAV copies to listen to

Needs numpy and ffmpeg (with libvorbis). The same files come out on every
run.
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import pieces  # noqa: E402
import sfx  # noqa: E402
import synth  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]


def encode(wav, ogg, quality):
    ogg.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(wav), "-c:a", "libvorbis", "-q:a", str(quality),
                    "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", str(ogg)], check=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "content"))
    ap.add_argument("--sfx", nargs="*", type=int, help="only these effects")
    ap.add_argument("--music", nargs="*", help="only these pieces")
    ap.add_argument("--wav", help="also keep WAV copies in this folder")
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    keep = pathlib.Path(args.wav) if args.wav else None
    if keep:
        keep.mkdir(parents=True, exist_ok=True)
    do_sfx = args.music is None or args.sfx is not None
    do_music = args.sfx is None or args.music is not None
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        if do_sfx:
            for voice, (name, _, make) in sfx.SOUNDS.items():
                if args.sfx and voice not in args.sfx:
                    continue
                sound = synth.trim(synth.normalize(np.asarray(make(1000 + voice)), sfx.level(voice)))
                wav = (keep or tmp) / f"{voice:03}-{name}.wav"
                synth.write_wav(wav, sound)
                encode(wav, out / "System/Game/Voice" / f"{voice:03}.ogg", 4)
                print(f"{voice:3} {name}")
        if do_music:
            for path, make in pieces.PIECES:
                piece = make()
                if args.music and piece.name not in args.music:
                    continue
                audio = pieces.render(piece) if hasattr(pieces, "render") else None
                wav = (keep or tmp) / f"music-{piece.name}.wav"
                synth.write_wav(wav, audio)
                encode(wav, out / path, 2)
                print(f"{path}: {piece.name}, {audio.shape[1] / synth.RATE:.1f} s")


if __name__ == "__main__":
    main()
