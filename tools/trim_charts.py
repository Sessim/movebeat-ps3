#!/usr/bin/env python3
"""Copy a level library keeping only the easiest charts of every level.

A personal package with many songs does not need every difficulty: this
copies each bundle of SOURCE into DESTINATION and keeps the KEEP easiest
charts of each level (Standard first, then other characteristics),
rewriting level.ini and conversion-report.json so the bundle still
validates. Playlist bundles are copied as they are; the PS3 picks the
nearest kept difficulty for a playlist song. SOURCE is never changed.

    python tools/trim_charts.py out/levels out/levels-pack --keep 2
    python tools/trim_charts.py raw out/levels-disney --keep 3 --max-difficulty Hard
    MOVEBEAT_LEVELS_DIR=out/levels-pack bash tools/build_ps3.sh --include-imported
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import shutil
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.bundle_format import sha256_file  # noqa: E402
from tools.validate_bundle import validate_bundle  # noqa: E402

DIFFICULTY_RANK = {"easy": 0, "normal": 1, "hard": 2, "expert": 3, "expertplus": 4}
CHART_KEY = re.compile(r"chart(\d+)_(\w+)")


def _chart_order(chart: dict[str, str]) -> tuple[int, int]:
    other = chart.get("characteristic", "Standard").casefold() != "standard"
    rank = DIFFICULTY_RANK.get(chart.get("difficulty", "").casefold(), 9)
    return (1 if other else 0, rank)


def trim_level(directory: Path, keep: int, max_difficulty: str | None = None) -> list[str]:
    """Keeps the `keep` easiest charts of a level bundle in place, none
    harder than `max_difficulty` (when a level has only harder ones, its
    easiest stays); returns the difficulties kept."""
    level_ini = directory / "level.ini"
    head: list[str] = []
    charts: dict[int, dict[str, str]] = {}
    for line in level_ini.read_text(encoding="utf-8").splitlines():
        key, _, value = line.partition("=")
        match = CHART_KEY.fullmatch(key)
        if match:
            charts.setdefault(int(match.group(1)), {})[match.group(2)] = value
        elif key != "chart_count":
            head.append(line)
    ordered = [charts[index] for index in sorted(charts)]
    candidates = sorted(ordered, key=_chart_order)
    # Lightshow charts carry lights and no notes: not a level to play.
    playable = [chart for chart in candidates
                if chart.get("characteristic", "").casefold() != "lightshow"]
    candidates = playable or candidates
    if max_difficulty:
        cap = DIFFICULTY_RANK[max_difficulty.casefold()]
        capped = [chart for chart in candidates
                  if DIFFICULTY_RANK.get(chart.get("difficulty", "").casefold(), 9) <= cap]
        candidates = capped or candidates[:1]
    kept = candidates[:keep]
    # Keep the original order of the kept charts (easiest first as imported).
    kept = [chart for chart in ordered if chart in kept]
    lines = head + [f"chart_count={len(kept)}"]
    for index, chart in enumerate(kept):
        for field, value in chart.items():
            lines.append(f"chart{index}_{field}={value}")
    level_ini.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    kept_files = {chart.get("file", "") for chart in kept}
    for chart in ordered:
        name = chart.get("file", "")
        if name and name not in kept_files:
            (directory / name).unlink(missing_ok=True)

    report_path = directory / "conversion-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if isinstance(report.get("charts"), list):
        report["charts"] = [chart for chart in report["charts"]
                            if chart.get("file") in kept_files]
    files = sorted((path for path in directory.iterdir()
                    if path.is_file() and path.name != report_path.name),
                   key=lambda path: path.name.casefold())
    report["files"] = [{"path": path.name, "size": path.stat().st_size,
                        "sha256": sha256_file(path)} for path in files]
    report_path.write_text(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                           encoding="utf-8")
    return [f"{chart.get('characteristic', '')}/{chart.get('difficulty', '')}" for chart in kept]


def trim_library(source: Path, destination: Path, keep: int, log=print,
                 max_difficulty: str | None = None) -> int:
    if destination.resolve() == source.resolve():
        raise ValueError("the destination must differ from the source library")
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    failures = 0
    for bundle in sorted(path for path in source.iterdir()
                         if path.is_dir() and not path.name.startswith(".")):
        target = destination / bundle.name
        shutil.copytree(bundle, target)
        if (target / "level.ini").is_file():
            kept = trim_level(target, keep, max_difficulty)
            note = ", ".join(kept)
        else:
            note = "copied"
        errors = validate_bundle(target)
        if errors:
            failures += 1
            log(f"{bundle.name}: INVALID after trimming: {'; '.join(errors)}")
        else:
            log(f"{bundle.name}: {note}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--keep", type=int, default=2, help="charts kept per level (default 2)")
    parser.add_argument("--max-difficulty", choices=["Easy", "Normal", "Hard", "Expert"],
                        help="drop charts harder than this")
    args = parser.parse_args()
    if args.keep < 1:
        parser.error("--keep must be at least 1")
    return 1 if trim_library(args.source, args.destination, args.keep,
                             max_difficulty=args.max_difficulty) else 0


if __name__ == "__main__":
    raise SystemExit(main())
