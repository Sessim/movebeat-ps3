from __future__ import annotations

import base64
import io
import json
from pathlib import Path
import tempfile
import unittest

from PIL import Image

from tools.beatsaver_import import ImportError, select_version
from tools.playlist_import import (
    PlaylistEntry, load_playlist, parse_bplist, playlist_id, resolve_entries,
    write_playlist_bundle,
)
from tools.validate_bundle import validate_bundle

HASH_A = "a" * 40
HASH_B = "b" * 40
HASH_C = "c" * 40


def png_data_uri() -> str:
    image = Image.new("RGB", (32, 32), (200, 40, 90))
    output = io.BytesIO()
    image.save(output, format="PNG")
    return "data:image/png;base64," + base64.b64encode(output.getvalue()).decode()


def bplist(**overrides: object) -> dict:
    document = {
        "playlistTitle": "Warmup Мix",
        "playlistAuthor": "Tester",
        "image": png_data_uri(),
        "songs": [
            {"key": "1A2B", "hash": HASH_A.upper(), "songName": "First",
             "difficulties": [{"characteristic": "Standard", "name": "expertPlus"}]},
            {"hash": HASH_B, "songName": "Second"},
            {"key": "3c4d", "songName": "Third"},
        ],
    }
    document.update(overrides)
    return document


def write_level(root: Path, name: str, key: str, content_hash: str) -> Path:
    directory = root / name
    directory.mkdir(parents=True)
    (directory / "level.ini").write_text(
        f"format=1\nsource_key={key}\nsource_hash={content_hash}\ntitle={name}\n",
        encoding="utf-8")
    return directory


class PlaylistParseTests(unittest.TestCase):
    def test_parses_songs_in_order_with_difficulty(self) -> None:
        playlist = parse_bplist(json.dumps(bplist()))
        self.assertEqual(playlist.title, "Warmup Мix")
        self.assertEqual([entry.title for entry in playlist.entries],
                         ["First", "Second", "Third"])
        self.assertEqual(playlist.entries[0].hash, HASH_A)
        self.assertEqual(playlist.entries[0].key, "1a2b")
        self.assertEqual(playlist.entries[0].difficulty, "ExpertPlus")
        self.assertEqual(playlist.entries[0].characteristic, "Standard")
        self.assertEqual(playlist.entries[1].difficulty, "")
        self.assertTrue(playlist.image and playlist.image.startswith(b"\x89PNG"))

    def test_rejects_bad_documents(self) -> None:
        for document in (bplist(songs=[]), bplist(playlistTitle=""),
                         bplist(songs=[{"songName": "No id"}]),
                         bplist(songs=[{"hash": "xyz"}])):
            with self.assertRaises(ImportError):
                parse_bplist(document)
        with self.assertRaises(ImportError):
            parse_bplist(b"not json")

    def test_ignores_unusable_images(self) -> None:
        self.assertIsNone(parse_bplist(bplist(image="bm90IGFuIGltYWdl")).image)
        self.assertIsNone(parse_bplist(bplist(image="%%%")).image)

    def test_playlist_ids(self) -> None:
        self.assertEqual(playlist_id("12345"), "12345")
        self.assertEqual(playlist_id("https://beatsaver.com/playlists/678"), "678")
        self.assertEqual(playlist_id("https://beatsaver.com/playlists/678/?x=1"), "678")
        self.assertIsNone(playlist_id("https://evil.example/playlists/678"))
        self.assertIsNone(playlist_id("http://beatsaver.com/playlists/678"))
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ImportError):
                load_playlist(str(Path(temporary) / "missing.txt"))

    def test_select_version_prefers_the_named_hash(self) -> None:
        versions = [
            {"hash": HASH_A, "downloadURL": "https://cdn.beatsaver.com/a.zip",
             "state": "Published", "createdAt": "2020-01-01T00:00:00Z"},
            {"hash": HASH_B, "downloadURL": "https://cdn.beatsaver.com/b.zip",
             "state": "Published", "createdAt": "2024-01-01T00:00:00Z"},
        ]
        self.assertEqual(select_version(versions)["hash"], HASH_B)
        self.assertEqual(select_version(versions, HASH_A.upper())["hash"], HASH_A)
        self.assertEqual(select_version(versions, HASH_C)["hash"], HASH_B)


class PlaylistBundleTests(unittest.TestCase):
    def test_resolves_library_and_writes_a_valid_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_level(root, "1a2b", "1a2b", HASH_A)
            # "Third" is in the library as another version of its map.
            write_level(root, "3c4d", "3c4d", HASH_C)
            fetched: list[str] = []

            def fake_fetch(entry: PlaylistEntry, output: Path) -> Path:
                fetched.append(entry.hash)
                return write_level(output, "bbbb", "bbbb", HASH_B)

            playlist = parse_bplist(bplist(), source_id="42")
            report = resolve_entries(playlist, root, fetch=fake_fetch, log=lambda _: None)
            self.assertEqual(fetched, [HASH_B])
            self.assertEqual(report.imported, ["bbbb"])
            self.assertEqual(sorted(report.existing), ["1a2b", "3c4d"])
            self.assertEqual(playlist.entries[2].hash, HASH_C)
            self.assertEqual(playlist.entries[1].key, "bbbb")

            bundle = write_playlist_bundle(playlist, root)
            self.assertEqual(bundle.name, "playlist-42")
            self.assertEqual(validate_bundle(bundle), [])
            ini = (bundle / "playlist.ini").read_text(encoding="utf-8")
            self.assertIn("title=Warmup Мix\n", ini)
            self.assertIn("song_count=3\n", ini)
            self.assertIn(f"song0_hash={HASH_A}\nsong0_key=1a2b\n", ini)
            self.assertIn("song0_difficulty=ExpertPlus\n", ini)
            self.assertIn(f"song2_hash={HASH_C}\n", ini)
            self.assertTrue((bundle / "cover.png").is_file())
            self.assertEqual((bundle / "library.mblc").stat().st_size, 8 + 512 * 96 * 2)
            with self.assertRaises(ImportError):
                write_playlist_bundle(playlist, root)
            self.assertEqual(write_playlist_bundle(playlist, root, force=True), bundle)

    def test_missing_maps_stay_out_of_the_bundle(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_level(root, "1a2b", "1a2b", HASH_A)
            playlist = parse_bplist(bplist(image=""), source_id="7")
            report = resolve_entries(playlist, root, download=False, log=lambda _: None)
            self.assertEqual(len(report.failed), 2)
            bundle = write_playlist_bundle(playlist, root)
            ini = (bundle / "playlist.ini").read_text(encoding="utf-8")
            self.assertIn("song_count=1\n", ini)
            self.assertFalse((bundle / "cover.png").exists())
            self.assertEqual(validate_bundle(bundle), [])

            empty = parse_bplist(bplist(songs=[{"hash": HASH_C}]), source_id="8")
            resolve_entries(empty, root, download=False, log=lambda _: None)
            with self.assertRaises(ImportError):
                write_playlist_bundle(empty, root)

    def test_validator_requires_the_playlist_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_level(root, "1a2b", "1a2b", HASH_A)
            playlist = parse_bplist(bplist(), source_id="9")
            resolve_entries(playlist, root, download=False, log=lambda _: None)
            bundle = write_playlist_bundle(playlist, root)
            (bundle / "playlist.ini").unlink()
            report = json.loads((bundle / "conversion-report.json").read_text(encoding="utf-8"))
            report["files"] = [entry for entry in report["files"]
                               if entry["path"] != "playlist.ini"]
            (bundle / "conversion-report.json").write_text(json.dumps(report), encoding="utf-8")
            self.assertIn("playlist.ini is not listed", validate_bundle(bundle))


if __name__ == "__main__":
    unittest.main()
