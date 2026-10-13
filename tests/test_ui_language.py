import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools import ui_language


class UiLanguageTests(unittest.TestCase):
    def setUp(self) -> None:
        language = ui_language.language()
        self.addCleanup(ui_language.set_language, language, save=False)

    def test_pairs_follow_the_language(self):
        ui_language.set_language("en", save=False)
        self.assertEqual(ui_language.tr("Готово", "Ready"), "Ready")
        ui_language.set_language("ru", save=False)
        self.assertEqual(ui_language.tr("Готово", "Ready"), "Готово")
        with self.assertRaises(ValueError):
            ui_language.set_language("de", save=False)

    def test_environment_then_saved_choice_then_windows(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(os.environ, {"APPDATA": directory, "MOVEBEAT_LANG": ""}):
                with patch.object(ui_language, "_windows_language", return_value="ru"):
                    self.assertEqual(ui_language.detect_language(), "ru")
                    ui_language.set_language("en")
                    self.assertEqual(
                        (Path(directory) / "MoveBeat" / "language.txt").read_text().strip(), "en")
                    self.assertEqual(ui_language.detect_language(), "en")
                    with patch.dict(os.environ, {"MOVEBEAT_LANG": "ru"}):
                        self.assertEqual(ui_language.detect_language(), "ru")

    def test_other_systems_default_to_english(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.dict(os.environ, {"APPDATA": directory, "MOVEBEAT_LANG": ""}):
                with patch.object(ui_language, "_windows_language", return_value=""):
                    self.assertEqual(ui_language.detect_language(), "en")


if __name__ == "__main__":
    unittest.main()
