import tempfile
import unittest
from pathlib import Path

from tools.playlist_import import parse_bplist
from tools.starter_set import STARTER_PLAYLISTS, import_starter_set


def entries():
    for source in STARTER_PLAYLISTS:
        yield from parse_bplist(source, source_id=source["id"]).entries


class StarterSetTests(unittest.TestCase):
    def test_every_song_names_a_key_and_a_hash_once(self):
        keys = [entry.key for entry in entries()]
        self.assertEqual(len(keys), 51)
        self.assertEqual(len(set(keys)), len(keys), "a song is in two playlists")
        for entry in entries():
            self.assertTrue(entry.key and len(entry.hash) == 40, entry)

    def test_maps_already_in_the_library_are_not_downloaded(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for entry in entries():
                level = root / entry.key
                level.mkdir()
                (level / "level.ini").write_text(
                    f"format=1\nsource_key={entry.key}\nsource_hash={entry.hash}\n",
                    encoding="utf-8")
            old = root / "playlist-starter"
            old.mkdir()
            (old / "playlist.ini").write_text("format=1\n", encoding="utf-8")
            bundles, report = import_starter_set(root, log=lambda _line: None)
            self.assertEqual((len(report.imported), len(report.existing), report.failed),
                             (0, 51, []))
            self.assertEqual([bundle.name for bundle in bundles],
                             ["playlist-" + source["id"] for source in STARTER_PLAYLISTS])
            text = (root / "playlist-throwback" / "playlist.ini").read_text(encoding="utf-8")
            self.assertIn("title=Throwback Hits", text)
            self.assertIn("song_count=13", text)
            self.assertFalse(old.exists(), "the first release's single list is replaced")


if __name__ == "__main__":
    unittest.main()
