#!/usr/bin/env python3
"""Build the version 3 sound library (docs/audio/<list>/NN.mp3) from the V2 SD-card recordings.

The V2 repository keeps its recordings as SDCard_files/A1..A10 (athans), F1..F10 (Fajr athans), D1..D10 (tawashih)
and B1..B10 (hourly ticks), in whatever form they were downloaded: 16 to 48 kHz, mono or stereo, 16 to 321 kbps,
with tags and cover art. Every one is rebuilt here into ONE uniform, plain format, so the clock never has to guess:

    MP3 (MPEG-1 Layer III), mono, 48 kHz (the clock's own output rate, so it never resamples),
    constant bitrate 64 kbps (lower only if a long file would not fit its size limit),
    no ID3 tags, no cover art, no Xing/LAME info frame: just audio frames.

The clock plays only the left channel (amp SD_MODE through 4.7 kOhm), and a mono file carries the whole sound there.

Per recording:

  1. Mono: a stereo recording is averaged (L+R)/2. If its two channels differ almost as much as they agree (wide
     or out-of-phase stereo, where averaging sounds hollow), the left channel alone is used instead.
  2. Level: one plain gain (no compression, no limiter) so the loudest moment sits near -1.5 dBFS, and the finished
     MP3, decoded, never peaks above -1 dBFS (MP3 coding overshoots a little; if it does, the gain is lowered by
     that much and the file encoded again). Nothing clips, and the recordings come out at similar loudness. Quiet
     files are raised by at most +20 dB.
  3. Resampled once to 48 kHz with a long, high-quality filter, then encoded.
  4. Checked: 48 kHz, mono, MP3, within the size and length limits, and exactly as long as the source (nothing is
     ever sped up or cut). A recording longer than its limit is refused; shorten it at the source.

Limits the clock enforces (README 1.5): athan, fajr, tawashih at most 3,000,000 bytes and 5:00; tick 400,000 bytes
and 1:00. C1-C4 (V2 menu tones) and Z_fallback_* are not part of the library.

Usage (needs ffmpeg and ffprobe; from the root of this repository, V2 repository next to it):
    python3 scripts/prepare_audio.py --check              # report only, write nothing
    python3 scripts/prepare_audio.py                      # rebuild docs/audio/athan/01.mp3 ... tick/10.mp3
    python3 scripts/prepare_audio.py --src /path/to/SDCard_files --out /tmp/audio_test

Names shown on the clock come from docs/audio/catalog.json (plain ASCII, at most 12 characters).
"""

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
LISTS = {"A": "athan", "F": "fajr", "D": "tawashih", "B": "tick"}
MAX_BYTES = {"athan": 3_000_000, "fajr": 3_000_000, "tawashih": 3_000_000, "tick": 400_000}
MAX_SECONDS = {"athan": 300, "fajr": 300, "tawashih": 300, "tick": 60}
RATE = 48000
BITRATES = (64, 56, 48, 40, 32)          # kbps, first that fits wins
PEAK_DB = -1.5                           # loudest sample after the gain, before encoding
DECODED_MAX_DB = -1.0                    # loudest sample of the finished MP3 when decoded (MP3 adds overshoot)
MAX_GAIN_DB = 20.0
WIDE_STEREO_DB = 4.0                     # mid within this of side: use the left channel instead of averaging
RESAMPLE = f"aresample={RATE}:filter_size=64:phase_shift=10:cutoff=0.97:linear_interp=1"


def run(args: list[str]) -> str:
    # errors="replace": ffmpeg echoes old tags, some in legacy Arabic encodings that are not UTF-8
    return subprocess.run(args, capture_output=True, text=True, errors="replace", check=True).stderr


def probe(path: pathlib.Path) -> dict:
    out = subprocess.run(["ffprobe", "-v", "error", "-print_format", "json", "-show_format", "-show_streams",
                          "-select_streams", "a:0", str(path)], capture_output=True, text=True, check=True).stdout
    j = json.loads(out)
    st = j["streams"][0]
    return {"duration": float(j["format"]["duration"]), "channels": int(st["channels"]),
            "rate": int(st["sample_rate"]), "codec": st["codec_name"], "bytes": path.stat().st_size}


def volume(src: pathlib.Path, chain: str, key: str) -> float:
    err = run(["ffmpeg", "-hide_banner", "-v", "info", "-i", str(src), "-vn", "-map", "0:a:0",
               "-af", f"{chain},volumedetect", "-f", "null", "-"])
    m = re.search(rf"{key}: (-?[\d.]+|-inf) dB", err)
    return -200.0 if m is None or m.group(1) == "-inf" else float(m.group(1))


def peak_db(src: pathlib.Path, chain: str) -> float:
    """True peak in dBFS, measured in floating point. volumedetect works in 16-bit and caps at 0 dB, which hides
    sources mastered too hot (A4 decodes to +5.5 dBFS: a 16-bit decoder hard-clips those samples)."""
    err = run(["ffmpeg", "-hide_banner", "-v", "info", "-i", str(src), "-vn", "-map", "0:a:0", "-af",
               f"{chain},aformat=sample_fmts=flt,astats=measure_perchannel=none:measure_overall=Peak_level",
               "-f", "null", "-"])
    m = re.search(r"Peak level dB: (-?[\d.]+|-inf)", err)
    return -200.0 if m is None or m.group(1) == "-inf" else float(m.group(1))


def mono_chain(src: pathlib.Path, channels: int) -> tuple[str, str]:
    if channels == 1:
        return "aformat=channel_layouts=mono", "mono"
    mid = volume(src, "pan=mono|c0=0.5*c0+0.5*c1", "mean_volume")
    side = volume(src, "pan=mono|c0=0.5*c0-0.5*c1", "mean_volume")
    if mid - side < WIDE_STEREO_DB:
        return "pan=mono|c0=c0", f"stereo, left channel only (wide stereo: mid {mid:.0f} dB, side {side:.0f} dB)"
    return "pan=mono|c0=0.5*c0+0.5*c1", "stereo, averaged"


def khz(rate: int) -> str:
    return f"{rate / 1000:g} kHz"


def build(src: pathlib.Path, dst: pathlib.Path, lst: str) -> tuple[bool, str]:
    info = probe(src)
    if info["duration"] > MAX_SECONDS[lst]:
        d = info["duration"]
        return False, f"{int(d // 60)}:{int(d % 60):02d} is over the {MAX_SECONDS[lst] // 60}:00 limit: shorten the source"
    mono, how = mono_chain(src, info["channels"])
    chain = f"{mono},{RESAMPLE}"
    peak = peak_db(src, chain)
    gain = min(PEAK_DB - peak, MAX_GAIN_DB)
    kbps = next((b for b in BITRATES if info["duration"] * b * 125 + 2000 <= MAX_BYTES[lst]), None)
    if kbps is None:
        return False, "too long to fit the size limit even at 32 kbps"
    for _ in range(4):
        subprocess.run(["ffmpeg", "-hide_banner", "-y", "-v", "error", "-i", str(src), "-vn", "-map", "0:a:0",
                        "-map_metadata", "-1", "-af", f"{chain},volume={gain:.2f}dB",
                        "-ar", str(RATE), "-ac", "1", "-c:a", "libmp3lame", "-b:a", f"{kbps}k",
                        "-write_xing", "0", "-id3v2_version", "0", "-f", "mp3", str(dst)], check=True)
        decoded_peak = peak_db(dst, "anull")
        if decoded_peak <= DECODED_MAX_DB:
            break
        gain -= decoded_peak - DECODED_MAX_DB + 0.3
    out = probe(dst)
    problems = []
    if out["codec"] != "mp3" or out["rate"] != RATE or out["channels"] != 1:
        problems.append(f"came out {out['codec']} {out['rate']} Hz {out['channels']} ch")
    if out["bytes"] > MAX_BYTES[lst]:
        problems.append(f"{out['bytes']} bytes, over the limit")
    if decoded_peak > DECODED_MAX_DB:
        problems.append(f"decoded peak {decoded_peak:.1f} dBFS")
    if abs(out["duration"] - info["duration"]) > 0.3:
        problems.append(f"length changed {info['duration']:.1f} s -> {out['duration']:.1f} s")
    note = (f"{out['bytes'] / 1e6:4.2f} MB  {int(out['duration'] // 60)}:{int(out['duration'] % 60):02d}  "
            f"{kbps} kbps  gain {gain:+5.1f} dB  peak {decoded_peak:5.1f} dBFS  from {khz(info['rate'])} {how}")
    return not problems, note + ("" if not problems else "  ✗ " + "; ".join(problems))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", type=pathlib.Path, default=ROOT.parent / "esp_athan" / "SDCard_files",
                    help="folder with A1..A10, F1..F10, D1..D10, B1..B10 (default: ../esp_athan/SDCard_files)")
    ap.add_argument("--out", type=pathlib.Path, default=ROOT / "docs" / "audio", help="default: docs/audio")
    ap.add_argument("--check", action="store_true", help="build into a temporary folder and report; write nothing")
    a = ap.parse_args()

    for tool in ("ffmpeg", "ffprobe"):
        if shutil.which(tool) is None:
            print(f"✗ {tool} not found (macOS: brew install ffmpeg)", file=sys.stderr)
            return 1
    if not a.src.is_dir():
        print(f"✗ source folder not found: {a.src}", file=sys.stderr)
        return 1

    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        out_root = pathlib.Path(tmp) if a.check else a.out
        for prefix, lst in LISTS.items():
            (out_root / lst).mkdir(parents=True, exist_ok=True)
            print(f"== {lst}  (limit {MAX_BYTES[lst] / 1e6:g} MB, {MAX_SECONDS[lst] // 60}:00)")
            for k in range(1, 11):
                src = a.src / f"{prefix}{k}.mp3"
                if not src.is_file():
                    print(f" ✗ {src.name:8} missing")
                    failed += 1
                    continue
                ok, note = build(src, out_root / lst / f"{k:02d}.mp3", lst)
                failed += 0 if ok else 1
                print(f" {'✓' if ok else '✗'} {src.name:8} → {lst + '/' + f'{k:02d}.mp3':16} {note}")
    print("\n(check only: nothing was written)" if a.check else f"\nWritten to {a.out}.")
    if failed:
        print(f"✗ {failed} file(s) missing or not usable", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
