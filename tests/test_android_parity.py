"""Compare the JVM-runnable Android converter with the desktop importer.

Set ORG_JSON_JAR to the pinned org.json:json:20250517 JAR. The test does not
download dependencies or require the Android SDK; AndroidCardJvmStub is used
for bundle parity, while the real RGB565 encoder is checked byte-for-byte.
Android Canvas text and cover scaling still require a device or emulator.
"""

from __future__ import annotations

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zipfile

from tests.test_beatsaver_import import OGG_FIXTURE, make_zip
from tools.beatsaver_import import import_archive
from tools.validate_bundle import validate_bundle


ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    ROOT / "android/app/src/main/java/io/movebeat/assistant/BeatSaverArchive.kt",
    ROOT / "android/app/src/main/java/io/movebeat/assistant/BeatSaverClient.kt",
    ROOT / "android/app/src/main/java/io/movebeat/assistant/BundleConverter.kt",
    ROOT / "android/app/src/main/java/io/movebeat/assistant/LibraryCardCodec.kt",
    ROOT / "android/app/src/main/java/io/movebeat/assistant/SearchSelection.kt",
    ROOT / "android/tools/AndroidCardJvmStub.kt",
    ROOT / "android/tools/BundleSmoke.kt",
    ROOT / "android/tools/CardCodecSmoke.kt",
    ROOT / "android/tools/SearchSelectionSmoke.kt",
]


def fixture(path: Path, version: int, audio_filename: str = "song.ogg") -> None:
    if version == 2:
        info = {
            "_version": "2.1.0", "_songName": "V2", "_songAuthorName": "Artist",
            "_levelAuthorName": "Mapper", "_songFilename": audio_filename,
            "_beatsPerMinute": 120, "_difficultyBeatmapSets": [{
                "_beatmapCharacteristicName": "Standard", "_difficultyBeatmaps": [
                    {"_difficulty": "Hard", "_beatmapFilename": "Hard.dat",
                     "_noteJumpMovementSpeed": 18,
                     "_noteJumpStartBeatOffset": 0.5}],
            }],
        }
        beatmap = {
            "_version": "2.0.0", "_notes": [
                {"_time": 2, "_lineIndex": 1, "_lineLayer": 2,
                 "_type": 0, "_cutDirection": 4, "_angleOffset": 15.5},
                {"_time": 3, "_lineIndex": 2, "_lineLayer": 1,
                 "_type": 3, "_cutDirection": 8}],
            "_obstacles": [{"_time": 5, "_duration": 2,
                            "_lineIndex": 1, "_width": 2, "_type": 0}],
            "_sliders": [{"_headTime": 7, "_tailTime": 8,
                          "_headLineIndex": 0, "_headLineLayer": 0,
                          "_headCutDirection": 0,
                          "_headControlPointLengthMultiplier": 1.5,
                          "_tailLineIndex": 3, "_tailLineLayer": 2,
                          "_tailCutDirection": 1,
                          "_tailControlPointLengthMultiplier": 0.5,
                          "_sliderMidAnchorMode": 2,
                          "_colorType": 0}],
        }
        chart_name = "Hard.dat"
    elif version == 3:
        info = {
            "_version": "2.1.0", "_songName": "V3", "_songFilename": audio_filename,
            "_beatsPerMinute": 120, "_difficultyBeatmapSets": [{
                "_beatmapCharacteristicName": "Standard", "_difficultyBeatmaps": [
                    {"_difficulty": "Expert", "_beatmapFilename": "Expert.dat",
                     "_noteJumpMovementSpeed": 14,
                     "_noteJumpStartBeatOffset": -0.25}],
            }],
        }
        beatmap = {
            "version": "3.2.0", "bpmEvents": [{"b": 2, "m": 60}],
            "colorNotes": [{"b": 3, "x": 0, "y": 0, "c": 1,
                            "d": 0, "a": -22.5}],
            "bombNotes": [{"b": 4, "x": 2, "y": 1}],
            "obstacles": [{"b": 1, "d": 2, "x": 0, "y": 2, "w": 1, "h": 3}],
            "burstSliders": [{"b": 5, "tb": 6, "x": 0, "y": 0,
                              "c": 0, "tx": 3, "ty": 2, "sc": 3, "s": 1.0}],
            "sliders": [{"b": 6, "tb": 7, "x": 3, "y": 2, "d": 4,
                         "mu": 1.25, "c": 1, "tx": 0, "ty": 0,
                         "tc": 7, "tmu": 0.75, "m": 1}],
        }
        chart_name = "Expert.dat"
    else:
        info = {
            "version": "4.0.0", "song": {"title": "V4"},
            "audio": {"songFilename": audio_filename, "bpm": 100},
            "difficultyBeatmaps": [{"characteristic": "Standard",
                                    "difficulty": "Easy", "beatmapDataFilename": "Easy.dat",
                                    "noteJumpMovementSpeed": 10,
                                    "noteJumpStartBeatOffset": 0.75}],
        }
        beatmap = {
            "version": "4.0.0", "colorNotes": [{"b": 1, "r": 0, "i": 0}],
            "colorNotesData": [{"x": 3, "y": 2, "c": 0, "d": 5, "a": 45},
                               {"x": 1, "y": 0, "c": 0, "d": 0}],
            "bombNotes": [{"b": 2, "r": 0, "i": 0}],
            "bombNotesData": [{"x": 1, "y": 0}],
            "obstacles": [{"b": 3, "r": 0, "i": 0}],
            "obstaclesData": [{"d": 1.5, "x": 2, "y": 0, "w": 2, "h": 5}],
            "chains": [{"hb": 4, "tb": 5, "hr": 0, "tr": 0, "i": 0, "ci": 0}],
            "chainsData": [{"tx": 0, "ty": 0, "c": 3, "s": 1.0}],
            "arcs": [{"hb": 6, "tb": 7, "hr": 0, "tr": 0,
                      "hi": 0, "ti": 1, "ai": 0}],
            "arcsData": [{"m": 1.5, "tm": 0.5, "a": 2}],
        }
        chart_name = "Easy.dat"
    make_zip(path, {"Info.dat": info, chart_name: beatmap,
                    audio_filename: OGG_FIXTURE})


class AndroidBundleParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        jar = os.environ.get("ORG_JSON_JAR")
        if not jar or not Path(jar).is_file():
            raise unittest.SkipTest("Set ORG_JSON_JAR to org.json:json:20250517")
        studio = Path(r"C:\Program Files\Android\Android Studio")
        kotlinc = Path(os.environ.get("KOTLINC", studio / "plugins/Kotlin/kotlinc/bin/kotlinc.bat"))
        java_home = Path(os.environ.get("JAVA_HOME", studio / "jre"))
        java = java_home / "bin/java.exe"
        stdlib = studio / "plugins/Kotlin/kotlinc/lib/kotlin-stdlib.jar"
        if not all(path.is_file() for path in (kotlinc, java, stdlib)):
            raise unittest.SkipTest("Kotlin CLI or Java runtime is unavailable")
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.work = Path(cls.temp.name)
        cls.java = java
        cls.classpath = os.pathsep.join((str(cls.work / "classes"), jar, str(stdlib)))
        env = dict(os.environ, JAVA_HOME=str(java_home))
        subprocess.run([str(kotlinc), "-jvm-target", "11", "-cp", jar,
                        "-d", str(cls.work / "classes"),
                        *(str(path) for path in SOURCES)],
                       cwd=ROOT, env=env, check=True, capture_output=True, text=True)

    def test_v2_v3_v4_match_desktop(self) -> None:
        for version in (2, 3, 4):
            with self.subTest(version=version):
                source = self.work / f"v{version}.zip"
                fixture(source, version)
                desktop = import_archive(source, self.work / f"desktop-{version}")
                bundle_zip = self.work / f"android-{version}.zip"
                subprocess.run([str(self.java), "-cp", self.classpath,
                                "io.movebeat.assistant.BundleSmokeKt",
                                str(source), str(bundle_zip)], cwd=ROOT,
                               check=True, capture_output=True, text=True)
                android_root = self.work / f"android-{version}"
                with zipfile.ZipFile(bundle_zip) as bundle:
                    bundle.extractall(android_root)
                android = next(android_root.iterdir())
                self.assertEqual(validate_bundle(android), [])
                njs, offset = {2: ("18", "0.5"), 3: ("14", "-0.25"),
                               4: ("10", "0.75")}[version]
                manifest_text = (android / "level.ini").read_text(encoding="utf-8")
                self.assertIn(f"chart0_njs={njs}", manifest_text)
                self.assertIn(f"chart0_start_beat_offset={offset}", manifest_text)
                names = ["level.ini", *(path.name for path in desktop.glob("*.mbchart"))]
                for name in names:
                    self.assertEqual((android / name).read_text(encoding="utf-8"),
                                     (desktop / name).read_text(encoding="utf-8"), name)
                import json
                left = json.loads((android / "conversion-report.json").read_text())
                right = json.loads((desktop / "conversion-report.json").read_text())
                for field in ("source", "level", "charts", "warnings"):
                    self.assertEqual(left[field], right[field], field)

    def test_card_rgb565_bytes_match_reference(self) -> None:
        card = self.work / "card.mblc"
        subprocess.run([str(self.java), "-cp", self.classpath,
                        "io.movebeat.assistant.CardCodecSmokeKt", str(card)],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        data = card.read_bytes()
        self.assertEqual(data[:8], b"MBLC" + struct.pack(">HH", 512, 96))
        self.assertEqual(len(data), 8 + 512 * 96 * 2)
        expected = bytearray()
        for index in range(512 * 96):
            red, green, blue = (index * 17) & 255, (index * 31) & 255, (index * 47) & 255
            value = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            expected.extend(struct.pack(">H", value))
        self.assertEqual(data[8:], expected)

    def test_egg_suffix_with_ogg_payload_is_preserved_and_reported(self) -> None:
        source = self.work / "egg-suffix.zip"
        fixture(source, 2, audio_filename="song.egg")
        desktop = import_archive(source, self.work / "desktop-egg")
        bundle_zip = self.work / "android-egg.zip"
        subprocess.run([str(self.java), "-cp", self.classpath,
                        "io.movebeat.assistant.BundleSmokeKt",
                        str(source), str(bundle_zip)], cwd=ROOT,
                       check=True, capture_output=True, text=True)
        android_root = self.work / "android-egg"
        with zipfile.ZipFile(bundle_zip) as bundle:
            bundle.extractall(android_root)
        android = next(android_root.iterdir())
        self.assertEqual(validate_bundle(android), [])
        self.assertEqual((android / "audio.egg").read_bytes(), OGG_FIXTURE)
        self.assertEqual((android / "audio.egg").read_bytes(),
                         (desktop / "audio.egg").read_bytes())

    def test_direct_lookup_selection_is_explicit_and_unambiguous(self) -> None:
        completed = subprocess.run(
            [str(self.java), "-cp", self.classpath,
             "io.movebeat.assistant.SearchSelectionSmokeKt"],
            cwd=ROOT, check=True, capture_output=True, text=True)
        self.assertIn("ANDROID_SEARCH_SELECTION=PASS", completed.stdout)

    def test_beatsaver_uploader_fills_missing_archive_mapper(self) -> None:
        source = self.work / "mapper-fallback.zip"
        fixture(source, 3)
        bundle_zip = self.work / "mapper-fallback-bundle.zip"
        subprocess.run([str(self.java), "-cp", self.classpath,
                        "io.movebeat.assistant.BundleSmokeKt",
                        str(source), str(bundle_zip), "54851", "BeatSaver Mapper"],
                       cwd=ROOT, check=True, capture_output=True, text=True)
        android_root = self.work / "mapper-fallback"
        with zipfile.ZipFile(bundle_zip) as bundle:
            bundle.extractall(android_root)
        android = next(android_root.iterdir())
        self.assertEqual(validate_bundle(android), [])
        self.assertIn("mapper=BeatSaver Mapper",
                      (android / "level.ini").read_text(encoding="utf-8"))
        import json
        report = json.loads((android / "conversion-report.json").read_text())
        self.assertEqual(report["level"]["mapper"], "BeatSaver Mapper")


if __name__ == "__main__":
    unittest.main()
