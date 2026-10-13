import tempfile
import unittest
from pathlib import Path

from tools.playlist_import import parse_bplist
from tools.starter_set import STARTER_PLAYLIST, import_starter_set


class StarterSetTests(unittest.TestCase):
    def test_every_song_names_a_key_and_a_hash(self):
        playlist = parse_bplist(STARTER_PLAYLIST, source_id="starter")
        self.assertEqual(len(playlist.entries), 6)
        for entry in playlist.entries:
            self.assertTrue(entry.key and len(entry.hash) == 40, entry)

    def test_maps_already_in_the_library_are_not_downloaded(self):
        playlist = parse_bplist(STARTER_PLAYLIST)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for entry in playlist.entries:
                level = root / entry.key
                level.mkdir()
                (level / "level.ini").write_text(
                    f"format=1\nsource_key={entry.key}\nsource_hash={entry.hash}\n",
                    encoding="utf-8")
            bundle, report = import_starter_set(root, log=lambda _line: None)
            self.assertEqual((len(report.imported), len(report.existing), report.failed),
                             (0, 6, []))
            text = (bundle / "playlist.ini").read_text(encoding="utf-8")
            self.assertIn("song_count=6", text)
            self.assertEqual(bundle.name, "playlist-starter")


if __name__ == "__main__":
    unittest.main()
