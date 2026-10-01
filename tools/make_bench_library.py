"""Generate a synthetic MoveBeat level library for menu benchmarks.

Every bundle is made from original content only: procedural charts, the
project's own First Steps audio and library cards rendered by
tools/library_card.py. Bundles are marked source.type "beat_saber_zip" so
the normal bundle validator and `build_ps3.sh --include-imported` accept
them; their SHA-1 fields are the SHA-1 of the generated files, and the key
and title say "bench". Use them only for local performance tests:

    python tools/make_bench_library.py --count 30 --output out/levels-bench
    MOVEBEAT_LEVELS_DIR=out/levels-bench bash tools/build_ps3.sh --include-imported
"""

from __future__ import annotations

import argparse
import hashlib
import json
import random
import shutil
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT))

from tools.bundle_format import BUNDLE_FORMAT_VERSION, sha256_file  # noqa: E402
from tools.library_card import write_card  # noqa: E402

FIRST_STEPS = PROJECT / "ps3/package/USRDIR/levels/movebeat_first_steps"
DIRECTIONS = ["U", "D", "L", "R", "UL", "UR", "DL", "DR", "A"]
DIFFICULTIES = [("Normal", 300), ("Hard", 600), ("Expert", 1000)]
TITLES = [
    "Neon Drift", "Glass Horizon", "Pulse Garden", "Static Bloom",
    "Night Relay", "Copper Wave", "Orbit Line", "Velvet Circuit",
    "Paper Comet", "Quiet Engine", "Amber Signal", "Cold Lantern",
    "Silver Steps", "Hollow Sky", "Ribbon Storm", "Echo Harbor",
    "Lumen Run", "Polar Tide", "Rust Parade", "Violet Hour",
    "Delta Bloom", "Ion Garden", "Kite Theory", "Moss Arcade",
    "Tin Aurora", "Wild Metric", "Zinc Season", "Bright Static",
    "Cloud Index", "Ember Code", "Frost Ladder", "Gamma Lane",
]
ARTISTS = ["MoveBeat Bench", "Synthetic Band", "Test Ensemble", "Procedural Trio"]


def make_chart(rng: random.Random, notes: int, bpm: float) -> str:
    beat_ms = 60000.0 / bpm
    lines = [
        "# notes: time_ms,lane,row,hand,direction",
        "# walls: W,time_ms,duration_ms,lane,width,row,height",
    ]
    time = 2000.0
    for index in range(notes):
        time += beat_ms * rng.choice([0.5, 0.5, 1.0, 1.0, 1.5])
        hand = "L" if index % 2 == 0 else "R"
        lane = rng.randint(0, 1) if hand == "L" else rng.randint(2, 3)
        lines.append(f"{int(time)},{lane},{rng.randint(0, 2)},{hand},"
                     f"{rng.choice(DIRECTIONS)}")
        if index % 40 == 39:
            lines.append(f"W,{int(time + beat_ms)},{int(beat_ms * 2)},"
                         f"{rng.choice([0, 3])},1,0,3")
    return "\n".join(lines) + "\n"


def make_bundle(output: Path, index: int) -> Path:
    rng = random.Random(index)
    key = f"bench{index:02d}"
    directory = output / key
    if directory.exists():
        shutil.rmtree(directory)
    directory.mkdir(parents=True)
    title = TITLES[(index - 1) % len(TITLES)]
    artist = ARTISTS[index % len(ARTISTS)]
    bpm = float(rng.choice([96, 110, 120, 128, 140, 150, 174]))
    shutil.copyfile(FIRST_STEPS / "audio.ogg", directory / "audio.ogg")
    ini = [
        "format=1", "source=beatsaver", f"source_key={key}",
        f"title={title}", f"song_author={artist}", "mapper=MoveBeat Bench",
        f"bpm={bpm:g}", "audio=audio.ogg", "cover=",
        f"chart_count={len(DIFFICULTIES)}",
    ]
    charts = []
    for chart_index, (difficulty, notes) in enumerate(DIFFICULTIES):
        name = f"Standard_{difficulty}.mbchart"
        (directory / name).write_text(make_chart(rng, notes, bpm),
                                      encoding="utf-8", newline="\n")
        ini += [
            f"chart{chart_index}_characteristic=Standard",
            f"chart{chart_index}_difficulty={difficulty}",
            f"chart{chart_index}_file={name}",
            f"chart{chart_index}_notes={notes}",
            f"chart{chart_index}_skipped=0",
        ]
        charts.append({"characteristic": "Standard", "difficulty": difficulty,
                       "file": name, "notes": notes, "obstacles": notes // 40,
                       "chains": 0, "arcs": 0, "skipped": 0})
    (directory / "level.ini").write_text("\n".join(ini) + "\n",
                                         encoding="utf-8", newline="\n")
    write_card(directory, "", title, artist, "MoveBeat Bench")
    files = sorted(path for path in directory.iterdir() if path.is_file())
    content = hashlib.sha1()
    for path in files:
        content.update(path.read_bytes())
    digest = content.hexdigest()
    report = {
        "bundleFormat": BUNDLE_FORMAT_VERSION,
        "charts": charts,
        "converter": {"name": "MoveBeat bench generator", "version": "1"},
        "files": [{"path": path.name, "size": path.stat().st_size,
                   "sha256": sha256_file(path)} for path in files],
        "level": {"audio": "audio.ogg", "audioCodec": "vorbis",
                  "audioContainer": "ogg", "bpm": bpm, "cover": "",
                  "mapper": "MoveBeat Bench", "songAuthor": artist,
                  "title": title, "libraryCard": "library.mblc"},
        "source": {"archiveSha1": digest, "contentSha1": digest, "key": key,
                   "type": "beat_saber_zip"},
        "warnings": ["synthetic benchmark bundle"],
    }
    (directory / "conversion-report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    return directory


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--output", type=Path,
                        default=PROJECT / "out/levels-bench")
    args = parser.parse_args()
    if not 1 <= args.count <= 200:
        parser.error("--count must be between 1 and 200")
    args.output.mkdir(parents=True, exist_ok=True)
    for index in range(1, args.count + 1):
        make_bundle(args.output, index)
    print(f"{args.count} bench bundles in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
