from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from tools.library_card import CARD_HEIGHT, CARD_WIDTH, render_card_bytes, write_card


class LibraryCardTests(unittest.TestCase):
    def test_unicode_mapper_is_pre_rendered_without_changing_card_size(self):
        basic = render_card_bytes(None, "Районы-кварталы", "Звери")
        mapped = render_card_bytes(None, "Районы-кварталы", "Звери", "Маппер")
        self.assertEqual(mapped[:8], b"MBLC\x02\x00\x00\x60")
        self.assertEqual(len(mapped), 8 + CARD_WIDTH * CARD_HEIGHT * 2)
        self.assertNotEqual(basic, mapped)

    def test_generated_card_is_named_consistently(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = write_card(Path(temporary), "", "First Steps", "MoveBeat",
                                "MoveBeat")
            self.assertEqual(output.name, "library.mblc")
            self.assertTrue(output.is_file())


if __name__ == "__main__":
    unittest.main()
