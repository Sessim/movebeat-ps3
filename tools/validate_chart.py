#!/usr/bin/env python3
"""Validate MoveBeat's deliberately small, PS3-friendly chart format."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import sys


@dataclass(frozen=True)
class Note:
    time_ms: int
    lane: int
    row: int
    hand: str
    direction: str


def parse_chart(path: Path) -> list[Note]:
    notes: list[Note] = []
    for number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) != 5:
            raise ValueError(f"{path}:{number}: expected 5 comma-separated fields")
        try:
            time_ms, lane, row = map(int, fields[:3])
        except ValueError as exc:
            raise ValueError(f"{path}:{number}: invalid integer") from exc
        hand, direction = fields[3:]
        if time_ms < 0:
            raise ValueError(f"{path}:{number}: time_ms must be non-negative")
        if lane not in range(4):
            raise ValueError(f"{path}:{number}: lane must be 0..3")
        if row not in range(3):
            raise ValueError(f"{path}:{number}: row must be 0..2")
        if hand not in {"L", "R", "B"}:
            raise ValueError(f"{path}:{number}: hand must be L, R or B")
        if direction not in {"U", "D", "L", "R", "UL", "UR", "DL", "DR", "A"}:
            raise ValueError(
                f"{path}:{number}: direction must be U, D, L, R, UL, UR, DL, DR or A"
            )
        notes.append(Note(time_ms, lane, row, hand, "A" if hand == "B" else direction))
    if not notes:
        raise ValueError(f"{path}: chart has no notes")
    return sorted(notes, key=lambda note: note.time_ms)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("charts", nargs="+", type=Path)
    args = parser.parse_args()
    failed = False
    for path in args.charts:
        try:
            notes = parse_chart(path)
            duration = notes[-1].time_ms / 1000.0
            print(f"OK {path}: {len(notes)} notes, {duration:.2f} s")
        except (OSError, UnicodeError, ValueError) as error:
            failed = True
            print(f"ERROR {error}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
