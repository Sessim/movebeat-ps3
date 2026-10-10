from __future__ import annotations

import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import tools.usb_export as usb
from tools.make_bench_library import make_bundle
from tools.validate_bundle import validate_bundle


def fat32(free: int = 1 << 34):
    return lambda root: usb.Volume(root, "FAT32", "USB", free)


class UsbExportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        base = Path(self.temporary.name)
        self.library = base / "levels"
        self.library.mkdir()
        self.drive = base / "drive"
        self.drive.mkdir()
        self.first = make_bundle(self.library, 1, key="usb01", title="Usb One",
                                 difficulties=[("Normal", 8)])
        self.second = make_bundle(self.library, 2, key="usb02", title="Usb Two",
                                  difficulties=[("Hard", 8)])

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def copied(self, name: str) -> Path:
        return self.drive / "MOVEBEAT" / "levels" / name

    def test_copies_verifies_and_publishes(self) -> None:
        progress = []
        destination = usb.export_bundle(self.first, self.drive, volume_reader=fat32(),
                                        progress=lambda done, total: progress.append((done, total)))
        self.assertEqual(destination, self.copied("usb01"))
        self.assertEqual(validate_bundle(destination), [])
        self.assertEqual(sorted(p.name for p in destination.iterdir()),
                         sorted(p.name for p in self.first.iterdir()))
        self.assertEqual(progress[-1][0], progress[-1][1])
        self.assertEqual([p.name for p in self.copied("").iterdir()
                          if p.name.startswith(".")], [])

    def test_never_replaces_an_existing_level(self) -> None:
        usb.export_bundle(self.first, self.drive, volume_reader=fat32())
        with self.assertRaises(usb.UsbExportError) as caught:
            usb.export_bundle(self.first, self.drive, volume_reader=fat32())
        self.assertIn("уже есть", str(caught.exception))

    def test_refuses_filesystems_the_ps3_cannot_read(self) -> None:
        for filesystem in ("exFAT", "NTFS"):
            with self.assertRaises(usb.UsbExportError) as caught:
                usb.export_bundle(self.first, self.drive, volume_reader=lambda root: usb.Volume(
                    root, filesystem, "USB", 1 << 34))
            self.assertIn("FAT32", str(caught.exception))
            self.assertIn(filesystem, str(caught.exception))
        self.assertFalse((self.drive / "MOVEBEAT").exists())

    def test_refuses_a_drive_without_room(self) -> None:
        with self.assertRaises(usb.UsbExportError) as caught:
            usb.export_bundle(self.first, self.drive, volume_reader=fat32(free=1024))
        self.assertIn("не хватает места", str(caught.exception))

    def test_a_bad_copy_leaves_nothing_behind(self) -> None:
        with patch.object(usb, "_sha256", return_value="0" * 64):
            with self.assertRaises(usb.UsbExportError) as caught:
                usb.export_bundle(self.first, self.drive, volume_reader=fat32())
        self.assertIn("Проверка скопированного файла", str(caught.exception))
        self.assertEqual(list(self.copied("").iterdir()), [])

    def test_exports_a_library_skipping_present_levels(self) -> None:
        usb.export_bundle(self.first, self.drive, volume_reader=fat32())
        report = usb.export_bundles(usb.library_bundles(self.library), self.drive,
                                    volume_reader=fat32())
        self.assertEqual(report.copied, ["usb02"])
        self.assertEqual([name for name, _ in report.skipped], ["usb01"])
        self.assertTrue(self.copied("usb02").is_dir())

    def test_refuses_an_invalid_bundle(self) -> None:
        (self.first / "level.ini").write_text("changed", encoding="utf-8")
        with self.assertRaises(usb.UsbExportError) as caught:
            usb.export_bundle(self.first, self.drive, volume_reader=fat32())
        self.assertIn("не прошёл проверку", str(caught.exception))

    @unittest.skipUnless(os.name == "nt", "Windows volume API")
    def test_reads_a_real_windows_volume(self) -> None:
        volume = usb.read_volume(Path(self.temporary.name))
        self.assertTrue(volume.filesystem)
        self.assertGreater(volume.free_bytes, 0)
        if volume.filesystem.upper() not in usb.PS3_FILESYSTEMS:
            with self.assertRaises(usb.UsbExportError):
                usb.check_ps3_volume(volume)
        self.assertIsInstance(usb.list_removable_drives(), list)


if __name__ == "__main__":
    unittest.main()
