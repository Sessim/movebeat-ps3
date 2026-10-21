import tempfile
import unittest
from pathlib import Path

from tools.make_bench_library import make_bundle
from tools.trim_charts import trim_library
from tools.validate_bundle import validate_bundle


class TrimChartsTests(unittest.TestCase):
    def test_keeps_the_two_easiest_charts_and_stays_valid(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "levels"
            # Listed hardest first, to check the order does not matter.
            make_bundle(source, 1, key="many", difficulties=[
                ("ExpertPlus", 40), ("Expert", 30), ("Hard", 20), ("Normal", 10)])
            make_bundle(source, 2, key="one", difficulties=[("Hard", 12)])
            before = (source / "many" / "level.ini").read_text(encoding="utf-8")
            failures = trim_library(source, root / "pack", 2, log=lambda _line: None)
            self.assertEqual(failures, 0)
            many = root / "pack" / "many"
            ini = (many / "level.ini").read_text(encoding="utf-8")
            self.assertIn("chart_count=2", ini)
            self.assertIn("difficulty=Hard", ini)
            self.assertIn("difficulty=Normal", ini)
            self.assertNotIn("Expert", ini)
            self.assertNotIn("chart2_", ini)
            self.assertEqual(sorted(path.name for path in many.glob("*.mbchart")),
                             ["Standard_Hard.mbchart", "Standard_Normal.mbchart"])
            self.assertEqual(validate_bundle(many), [])
            self.assertIn("chart_count=1", (root / "pack" / "one" / "level.ini")
                          .read_text(encoding="utf-8"))
            # The source library is left alone.
            self.assertEqual((source / "many" / "level.ini").read_text(encoding="utf-8"), before)

    def test_drops_charts_harder_than_the_cap(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "levels"
            make_bundle(source, 1, key="mixed", difficulties=[
                ("Easy", 10), ("Hard", 20), ("Expert", 30), ("ExpertPlus", 40)])
            make_bundle(source, 2, key="hardest", difficulties=[("ExpertPlus", 40), ("Expert", 30)])
            failures = trim_library(source, root / "pack", 3, log=lambda _line: None,
                                    max_difficulty="Hard")
            self.assertEqual(failures, 0)
            ini = (root / "pack" / "mixed" / "level.ini").read_text(encoding="utf-8")
            self.assertIn("chart_count=2", ini)
            self.assertNotIn("Expert", ini)
            # A level with only harder charts keeps its easiest one.
            hardest = (root / "pack" / "hardest" / "level.ini").read_text(encoding="utf-8")
            self.assertIn("chart_count=1", hardest)
            self.assertIn("difficulty=Expert\n", hardest)

    def test_refuses_to_trim_in_place(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                trim_library(Path(directory), Path(directory), 2)


if __name__ == "__main__":
    unittest.main()
