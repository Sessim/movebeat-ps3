#!/usr/bin/env python3
"""Download MoveBeat's starter set of BeatSaver maps as a playlist bundle.

The release package carries only the built-in tutorial: the starter maps'
music belongs to its rightsholders, so each player downloads them from
BeatSaver. The set is an ordinary playlist (keys and content hashes); maps
already in the library are kept, and the playlist bundle lands next to them
as playlist-starter:

    python tools/starter_set.py
    python tools/starter_set.py --output D:/MoveBeat/levels
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
from typing import Callable

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.playlist_import import (  # noqa: E402
    PlaylistImportReport, parse_bplist, resolve_entries, write_playlist_bundle,
)


STARTER_PLAYLIST = {
    "playlistTitle": "MoveBeat Starter",
    "playlistAuthor": "MoveBeat",
    "songs": [
        {"key": "132ce", "hash": "2df6be39f87aff225e86a83e5185aa1de8264e18",
         "songName": "Believer"},
        {"key": "27607", "hash": "02ddcc1b044b82199944a8d6ec83f3024c06e102",
         "songName": "Kukla Koldyna"},
        {"key": "3b31", "hash": "46afff5698b59795c5e88f0b610b8321fb9b1791",
         "songName": "Hit the Road Jack (Electro Swing)"},
        {"key": "3bb9b", "hash": "c5bb3de149cf527989b427f7ea002e40b888945c",
         "songName": "Mozhno ya s toboj"},
        {"key": "425de", "hash": "a624059126147955376e77e3b07b8690b9fff91e",
         "songName": "Durak I Molniya"},
        {"key": "4f7e3", "hash": "ed6646365ed086d1c3a93a0733441b50719a81f1",
         "songName": "Primo Victoria"},
    ],
}


def import_starter_set(output_root: Path, *, log: Callable[[str], None] = print
                       ) -> tuple[Path, PlaylistImportReport]:
    """Make sure the starter maps are in the library; write their playlist."""
    playlist = parse_bplist(STARTER_PLAYLIST, source_id="starter")
    log(f"{playlist.title}: {len(playlist.entries)} maps")
    report = resolve_entries(playlist, output_root, log=log)
    return write_playlist_bundle(playlist, output_root, force=True), report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=Path("out/levels"))
    args = parser.parse_args()
    bundle, report = import_starter_set(args.output)
    print(f"Playlist bundle: {bundle}")
    print(f"Downloaded: {len(report.imported)}, already there: {len(report.existing)}, "
          f"failed: {len(report.failed)}")
    for label, reason in report.failed:
        print(f"  {label}: {reason}")
    print("Use downloaded content only as permitted by its rightsholders and BeatSaver's terms.")
    return 1 if report.failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
