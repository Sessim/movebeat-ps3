from __future__ import annotations

from pathlib import Path
import json
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from assistant.movebeat_assistant import default_output_root, find_project_root, wsl_path
from assistant.ftp_profile import load_ftp_profile, save_ftp_profile


class TrackAssistantTests(unittest.TestCase):
    def test_finds_current_movebeat_project(self) -> None:
        root = find_project_root()
        self.assertIsNotNone(root)
        assert root is not None
        self.assertTrue((root / "tools" / "build_ps3.sh").is_file())

    def test_project_default_library_is_out_levels(self) -> None:
        root = Path("C:/MoveBeat")
        self.assertEqual(default_output_root(root), root / "out" / "levels")

    def test_wsl_path_rejects_non_absolute_linux_result(self) -> None:
        completed = subprocess.CompletedProcess([], 0, stdout="relative/path\n", stderr="")
        with patch("assistant.movebeat_assistant.subprocess.run", return_value=completed):
            with self.assertRaisesRegex(RuntimeError, "invalid project path"):
                wsl_path(Path("C:/MoveBeat"))

    def test_ftp_profile_keeps_only_non_secret_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ftp-profile.json"
            save_ftp_profile(path, " 192.168.1.9 ", 2121, " ps3 ")
            self.assertEqual(load_ftp_profile(path), ("192.168.1.9", 2121, "ps3"))
            self.assertEqual(set(json.loads(path.read_text(encoding="utf-8"))),
                             {"format", "host", "port", "user"})
            self.assertNotIn("password", path.read_text(encoding="utf-8"))

    def test_invalid_ftp_profile_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ftp-profile.json"
            with self.assertRaises(ValueError):
                save_ftp_profile(path, "", 21, "anonymous")
            self.assertFalse(path.exists())
            path.write_text('{"format":1,"host":"ps3","port":0}', encoding="utf-8")
            with self.assertRaises(ValueError):
                load_ftp_profile(path)
            path.write_text('{"format":1,"host":42,"port":21}', encoding="utf-8")
            with self.assertRaises(ValueError):
                load_ftp_profile(path)


if __name__ == "__main__":
    unittest.main()
