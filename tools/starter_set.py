#!/usr/bin/env python3
"""Download MoveBeat's starter set of BeatSaver maps as playlist bundles.

The release package carries only the built-in tutorial: the starter maps'
music belongs to its rightsholders, so each player downloads them from
BeatSaver. The set is a few themed playlists (keys and content hashes);
maps already in the library are kept, and each playlist bundle lands next
to them as playlist-<id>:

    python tools/starter_set.py
    python tools/starter_set.py --output D:/MoveBeat/levels
"""

from __future__ import annotations

import argparse
import io
from pathlib import Path
import shutil
import sys
from typing import Callable

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.playlist_import import (  # noqa: E402
    Playlist, PlaylistImportReport, library_index, parse_bplist, resolve_entries,
    write_playlist_bundle,
)


STARTER_PLAYLISTS = [
    {"id": "throwback", "playlistTitle": "Throwback Hits",
     "playlistAuthor": "MoveBeat: 70s to 00s", "songs": [
        {"key": "3418b", "hash": "36e287cfd42c932ba6900570546c0d49bb23798c", "songName": "Have You Ever Seen The Rain?"},
        {"key": "37c19", "hash": "88ce37b0a10759e67f6e6b9d6b43d641e950b035", "songName": "Last Christmas"},
        {"key": "4fed6", "hash": "b810a85827e9f37682ab1ca4943a3023a351707f", "songName": "U Can't Touch This"},
        {"key": "461f3", "hash": "741833d14d890f22e734f457bc60bd4b1c99de22", "songName": "Thunderstruck"},
        {"key": "53834", "hash": "201f652058f0676cd6f3a1e27a9f67f217fb5717", "songName": "Boom, Boom, Boom, Boom"},
        {"key": "342b1", "hash": "8192bda9a875a958be8f549ab1c9f70e50cd8e68", "songName": "Mambo Number 5"},
        {"key": "537be", "hash": "61c8cb25a756ab468ece58586c2977fe4534b468", "songName": "Livin' la Vida Loca"},
        {"key": "3f5bd", "hash": "4cae5ce85242f26061635ea864ec5dbaf05db5de", "songName": "Genie in a Bottle"},
        {"key": "29cf4", "hash": "a1e802262136014c066166a6c33b8c7af64ebb13", "songName": "Can't Get You out of My Head"},
        {"key": "21540", "hash": "04a5b1557faa6fa56453468fa42fe1c1eaf17c67", "songName": "Toxic"},
        {"key": "47899", "hash": "37a33b1401375109cc586326781229048c0b3d75", "songName": "Bring Me To Life"},
        {"key": "46c2e", "hash": "53c29d5ee5e0401f92144008c0507b0d841c9ef6", "songName": "Bad Day"},
        {"key": "33cfe", "hash": "ea2ffd91885dd1073bef0de114c12b68ad5b6404", "songName": "You're Gonna Go Far, Kid"},
    ]},
    {"id": "party", "playlistTitle": "Party Hits",
     "playlistAuthor": "MoveBeat: 2010s dance floor", "songs": [
        {"key": "26df0", "hash": "e167a0cf52d8433b051a1f95a60f2b1b6a43c834", "songName": "I Know You Want Me"},
        {"key": "27afd", "hash": "f6ded0f5dffeb4eb6b8f6b995c3f37663be6de77", "songName": "Show Me How You Burlesque"},
        {"key": "22c4d", "hash": "5c56fa44acbf6daf876fd3deb4fb8b6598c2377c", "songName": "Run Away [Fitbeat] (Epic Sax Guy)"},
        {"key": "54179", "hash": "0cc7c413897ab8c114140c73026ffb21fe73fc6c", "songName": "On The Floor"},
        {"key": "5268d", "hash": "01dfd6ee614fb87e526f8ca5a501c2b49e22205a", "songName": "Call Me Maybe"},
        {"key": "24923", "hash": "db4c4ddf5e8c6d5dc41571ab701d416852c1312a", "songName": "La La La"},
        {"key": "49707", "hash": "ac772dbc0ce267d676ba39dd2c72f5cadeb019d1", "songName": "Me and My Broken Heart"},
        {"key": "36a11", "hash": "b3a70105d0d8d48cf963e29230243e6c1dffdbe6", "songName": "Sugar"},
        {"key": "4538d", "hash": "97a9772f887772dd70f4abbc9f090bc86c7bafce", "songName": "See You Again"},
        {"key": "2ab5b", "hash": "3da135c11207f5f4f2dfe7b18a6416931f66936e", "songName": "Handclap"},
        {"key": "3b31", "hash": "46afff5698b59795c5e88f0b610b8321fb9b1791", "songName": "Hit the Road Jack (Electro Swing)"},
        {"key": "22c5a", "hash": "2fdc83f701e6a59f88bb3a135ca9c3f8c88bef61", "songName": "Friend Like Me"},
        {"key": "259b4", "hash": "9888a63d17bc5647f7a19c7a189e5bd4af15b7b9", "songName": "Friend Like Me"},
    ]},
    {"id": "fresh", "playlistTitle": "Fresh Hits",
     "playlistAuthor": "MoveBeat: 2020s", "songs": [
        {"key": "227c6", "hash": "b0a0796652b19affe0b0dd63a9feb1108146d0ad", "songName": "Bad Habits"},
        {"key": "52134", "hash": "cc7479e78fa3c989bc7203b0c5786802308001ba", "songName": "[Fitbeat] Physical"},
        {"key": "54c9e", "hash": "238cd7b85c1169e1f0a860cc24e6be494268608f", "songName": "Take You To Hell"},
        {"key": "2cb61", "hash": "9f487805094dd6785d7e84e7f34c35ed6371dd7f", "songName": "Ferrari"},
        {"key": "3bcb2", "hash": "d0e6802031ee00d184af9cfb44b49ddd0d0fbc3c", "songName": "Espresso"},
        {"key": "43d05", "hash": "149db1f8649e8ae04862cf9002b47c2ffd139c35", "songName": "APT."},
        {"key": "40a1d", "hash": "cf20daf9fe248fd5b6c66016c3345982bb0d1fca", "songName": "Pedro (Remix)"},
        {"key": "3e3f7", "hash": "14397ba40c5f6a4a68728bc08c6b777bd7fdd3f3", "songName": "it boy"},
        {"key": "3e7e6", "hash": "8b013732138110a6b3bde0e5ed9f7218d8d578b0", "songName": "can't slow down"},
        {"key": "33d77", "hash": "1338abc5881b9ba791fa6d8a3fbc47cc3389de74", "songName": "GET GET DOWN"},
    ]},
    {"id": "anthems", "playlistTitle": "Pop Rock Anthems",
     "playlistAuthor": "MoveBeat: Imagine Dragons and friends", "songs": [
        {"key": "132ce", "hash": "2df6be39f87aff225e86a83e5185aa1de8264e18", "songName": "beliver"},
        {"key": "22ee8", "hash": "9e06817f5f2a9c1b6e163b11ddd1e1fe9d840c94", "songName": "Bones"},
        {"key": "24948", "hash": "a57d19c690e88e632a65c4cabb3a12bd41ec298d", "songName": "Bad Liar"},
        {"key": "42a0a", "hash": "2862f1a372a3823b2dcf194806aa0e8564bbbc98", "songName": "Take Me to the Beach"},
        {"key": "3833c", "hash": "2cf8f671bf714e04382032a6e97a9003f420529f", "songName": "Viva La Vida"},
        {"key": "1e1e5", "hash": "49901af2e13fa4ffa2dc27c55e7256c9feeec9c8", "songName": "Move"},
    ]},
    {"id": "electro", "playlistTitle": "Electro and Memes",
     "playlistAuthor": "MoveBeat: Beats to slash", "songs": [
        {"key": "30560", "hash": "44b327b7127d1e04875af2afd3cf201d6bcefd2c", "songName": "Crab Rave"},
        {"key": "3f92d", "hash": "ee9df0b1c7d43bb27783c2a00fea0a2d4758a43d", "songName": "U Got That"},
        {"key": "3df54", "hash": "bde26148adb72e2fc4631a8d217b8aa1f9623ae1", "songName": "Steps"},
        {"key": "3e798", "hash": "ded00f86b995018ea0d35e54554b1848c6afca26", "songName": "Inhale"},
        {"key": "4107e", "hash": "589dff876aed7381977d954fbb56764896116d74", "songName": "Skibidi"},
        {"key": "206c5", "hash": "23c9f044b42bcd3fdd5a467bc21bb6b78cafbd61", "songName": "Freaks [Fitbeat]"},
    ]},
    {"id": "russian", "playlistTitle": "Russian Rock and Rap",
     "playlistAuthor": "MoveBeat: Korol i Shut, Ap$ent", "songs": [
        {"key": "27607", "hash": "02ddcc1b044b82199944a8d6ec83f3024c06e102", "songName": "Kukla Koldyna"},
        {"key": "425de", "hash": "a624059126147955376e77e3b07b8690b9fff91e", "songName": "Durak I Molniya"},
        {"key": "3bb9b", "hash": "c5bb3de149cf527989b427f7ea002e40b888945c", "songName": "Mozhno ya s toboj"},
    ]},
]


def collage(playlist: Playlist, output_root: Path, size: int = 256) -> bytes | None:
    """A 2x2 cover of the playlist's first four song covers (repeated when fewer)."""
    from PIL import Image, UnidentifiedImageError

    by_hash, by_key = library_index(output_root)
    covers: list[Path] = []
    for entry in playlist.entries:
        name = by_hash.get(entry.hash) or by_key.get(entry.key, ("", ""))[1]
        for cover in ((output_root / name / "cover.jpg"), (output_root / name / "cover.png")):
            if name and cover.is_file():
                covers.append(cover)
                break
        if len(covers) == 4:
            break
    if not covers:
        return None
    covers = (covers * 4)[:4]
    tile = size // 2
    canvas = Image.new("RGB", (size, size))
    for index, path in enumerate(covers):
        try:
            with Image.open(path) as image:
                piece = image.convert("RGB").resize((tile, tile), Image.LANCZOS)
        except (OSError, UnidentifiedImageError):
            return None
        canvas.paste(piece, ((index % 2) * tile, (index // 2) * tile))
    output = io.BytesIO()
    canvas.save(output, "JPEG", quality=90)
    return output.getvalue()


def import_starter_set(output_root: Path, *, log: Callable[[str], None] = print
                       ) -> tuple[list[Path], PlaylistImportReport]:
    """Make sure the starter maps are in the library; write their playlists."""
    report = PlaylistImportReport()
    bundles: list[Path] = []
    for source in STARTER_PLAYLISTS:
        playlist = parse_bplist(source, source_id=source["id"])
        log(f"{playlist.title}: {len(playlist.entries)} maps")
        part = resolve_entries(playlist, output_root, log=log)
        report.imported += part.imported
        report.existing += part.existing
        report.failed += part.failed
        if any(entry.available for entry in playlist.entries):
            playlist.image = collage(playlist, output_root)
            bundles.append(write_playlist_bundle(playlist, output_root, force=True))
    # The single list of the first release, replaced by the themed ones.
    old = output_root / "playlist-starter"
    if (old / "playlist.ini").is_file():
        shutil.rmtree(old)
    return bundles, report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", type=Path, default=Path("out/levels"))
    args = parser.parse_args()
    bundles, report = import_starter_set(args.output)
    for bundle in bundles:
        print(f"Playlist bundle: {bundle}")
    print(f"Downloaded: {len(report.imported)}, already there: {len(report.existing)}, "
          f"failed: {len(report.failed)}")
    for label, reason in report.failed:
        print(f"  {label}: {reason}")
    print("Use downloaded content only as permitted by its rightsholders and BeatSaver's terms.")
    return 1 if report.failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
