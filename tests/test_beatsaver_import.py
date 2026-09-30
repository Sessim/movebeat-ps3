from __future__ import annotations

import json
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from PIL import Image

import tools.beatsaver_import as importer
from tools.beatsaver_import import ImportError, import_archive, search_maps, select_version
from tools.validate_bundle import validate_bundle


OGG_FIXTURE = b"OggS" + b"\0" * 24 + b"\x01vorbis" + b"\0" * 16


def make_zip(path: Path, files: dict[str, object]) -> None:
    with zipfile.ZipFile(path, "w") as archive:
        for name, value in files.items():
            if isinstance(value, (dict, list)):
                archive.writestr(name, json.dumps(value))
            else:
                archive.writestr(name, value)


def zip_bytes(files: dict[str, object]) -> bytes:
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as archive:
        for name, value in files.items():
            if isinstance(value, (dict, list)):
                archive.writestr(name, json.dumps(value))
            else:
                archive.writestr(name, value)
    return output.getvalue()


class BeatSaverImportTests(unittest.TestCase):
    def test_selects_newest_published_version_independent_of_api_order(self) -> None:
        versions = [
            {"state": "Published", "createdAt": "2025-01-02T00:00:00Z", "downloadURL": "https://r2cdn.beatsaver.com/new.zip"},
            {"state": "Uploaded", "createdAt": "2026-01-01T00:00:00Z", "downloadURL": "https://r2cdn.beatsaver.com/draft.zip"},
            {"state": "Published", "createdAt": "2024-01-02T00:00:00Z", "downloadURL": "https://r2cdn.beatsaver.com/old.zip"},
        ]
        self.assertEqual(select_version(versions)["downloadURL"], "https://r2cdn.beatsaver.com/new.zip")

    def test_download_rejects_sha1_mismatch_and_removes_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "map.zip"
            metadata = {
                "id": "test", "uploader": {"name": "mapper"},
                "versions": [{
                    "state": "Published", "createdAt": "2026-01-01T00:00:00Z",
                    "downloadURL": "https://r2cdn.beatsaver.com/test.zip",
                    "hash": "0" * 40,
                }],
            }
            body = zip_bytes({
                "Info.dat": {
                    "_version": "2.1.0", "_songFilename": "song.ogg",
                    "_beatsPerMinute": 120,
                    "_difficultyBeatmapSets": [{
                        "_beatmapCharacteristicName": "Standard",
                        "_difficultyBeatmaps": [{
                            "_difficulty": "Easy", "_beatmapFilename": "Easy.dat"}],
                    }],
                },
                "Easy.dat": {"_version": "2.0.0", "_notes": []},
                "song.ogg": OGG_FIXTURE,
            })
            response = unittest.mock.MagicMock()
            response.__enter__.return_value = response
            response.read.side_effect = [body, b""]
            with patch.object(importer, "request_json", return_value=metadata), \
                    patch.object(importer.urllib.request, "urlopen", return_value=response):
                with self.assertRaisesRegex(ImportError, "SHA-1"):
                    importer.download_map("test", destination)
            self.assertFalse(destination.exists())

    def test_download_can_be_cancelled_and_removes_partial_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "map.zip"
            metadata = {
                "id": "test", "versions": [{
                    "state": "Published", "createdAt": "2026-01-01T00:00:00Z",
                    "downloadURL": "https://r2cdn.beatsaver.com/test.zip",
                    "hash": "0" * 40,
                }],
            }
            response = unittest.mock.MagicMock()
            response.__enter__.return_value = response
            response.headers = {"Content-Length": "100"}
            with patch.object(importer, "request_json", return_value=metadata), \
                    patch.object(importer.urllib.request, "urlopen", return_value=response):
                with self.assertRaisesRegex(ImportError, "cancelled"):
                    importer.download_map("test", destination, cancelled=lambda: True)
            self.assertFalse(destination.exists())

    def test_search_returns_importable_keys_and_difficulties(self) -> None:
        response = {"docs": [{
            "id": "abc", "name": "A Song", "uploader": {"name": "Mapper"},
            "versions": [{
                "state": "Published", "createdAt": "2026-01-01T00:00:00Z",
                "downloadURL": "https://r2cdn.beatsaver.com/abc.zip",
                "diffs": [
                    {"characteristic": "Standard", "difficulty": "Hard"},
                    {"characteristic": "Standard", "difficulty": "Expert"},
                ],
            }],
        }]}
        with patch.object(importer, "request_json", return_value=response) as request:
            results = search_maps("song & artist", 5)
        self.assertEqual(results[0].key, "abc")
        self.assertEqual(results[0].title, "A Song")
        self.assertEqual(results[0].mapper, "Mapper")
        self.assertEqual(results[0].difficulties, ("Standard:Hard", "Standard:Expert"))
        requested_url = request.call_args.args[0]
        self.assertIn("q=song+%26+artist", requested_url)
        self.assertIn("pageSize=5", requested_url)

    def test_bsr_command_resolves_direct_map_id(self) -> None:
        response = {
            "id": "54851", "name": "Like Animals", "uploader": {"name": "clay.nix"},
            "versions": [{
                "state": "Published", "createdAt": "2026-01-01T00:00:00Z",
                "downloadURL": "https://r2cdn.beatsaver.com/map.zip",
                "diffs": [{"characteristic": "Standard", "difficulty": "Expert"}],
            }],
        }
        with patch.object(importer, "request_json", return_value=response) as request:
            results = search_maps("!bsr 54851", 20)
        self.assertEqual(results[0].key, "54851")
        self.assertEqual(results[0].title, "Like Animals")
        self.assertEqual(results[0].difficulties, ("Standard:Expert",))
        self.assertEqual(request.call_args.args[0], "https://api.beatsaver.com/maps/id/54851")

    def test_plain_numeric_map_id_uses_direct_lookup(self) -> None:
        response = {"id": "54851", "name": "Like Animals", "versions": []}
        with patch.object(importer, "request_json", return_value=response) as request:
            results = search_maps("54851")
        self.assertEqual(results[0].key, "54851")
        self.assertIn("/maps/id/54851", request.call_args.args[0])

    def test_v2_map_and_diagonal_directions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "v2.zip"
            cover_data = io.BytesIO()
            Image.new("RGB", (12, 12), (42, 100, 200)).save(cover_data,
                                                              format="PNG")
            make_zip(source, {
                "Info.dat": {
                    "_version": "2.1.0", "_songName": "V2", "_songFilename": "song.ogg",
                    "_coverImageFilename": "cover.png", "_beatsPerMinute": 120,
                    "_difficultyBeatmapSets": [{
                        "_beatmapCharacteristicName": "Standard",
                        "_difficultyBeatmaps": [{
                            "_difficulty": "Hard", "_beatmapFilename": "Hard.dat",
                            "_noteJumpMovementSpeed": 18,
                            "_noteJumpStartBeatOffset": 0.5}],
                    }],
                },
                "Hard.dat": {"_version": "2.0.0", "_notes": [
                    {"_time": 2, "_lineIndex": 1, "_lineLayer": 2, "_type": 0,
                     "_cutDirection": 4, "_angleOffset": 15.5},
                    {"_time": 3, "_lineIndex": 2, "_lineLayer": 1, "_type": 1, "_cutDirection": 7},
                    {"_time": 4, "_lineIndex": 2, "_lineLayer": 1, "_type": 3, "_cutDirection": 0},
                ], "_obstacles": [
                    {"_time": 5, "_duration": 2, "_lineIndex": 1,
                     "_width": 2, "_type": 0},
                ], "_sliders": [
                    {"_headTime": 7, "_tailTime": 8,
                     "_headLineIndex": 0, "_headLineLayer": 0,
                     "_headCutDirection": 0,
                     "_headControlPointLengthMultiplier": 1.5,
                     "_tailLineIndex": 3, "_tailLineLayer": 2,
                     "_tailCutDirection": 1,
                     "_tailControlPointLengthMultiplier": 0.5,
                     "_sliderMidAnchorMode": 2,
                     "_colorType": 0},
                ]},
                "song.ogg": OGG_FIXTURE, "cover.png": cover_data.getvalue(),
            })
            result = import_archive(source, root / "out")
            chart = (result / "Standard_Hard.mbchart").read_text(encoding="utf-8")
            self.assertIn("1000,1,2,L,UL,15.5", chart)
            self.assertIn("1500,2,1,R,DR", chart)
            self.assertIn("2000,2,1,B,A", chart)
            self.assertIn("W,2500,1000,1,2,0,3", chart)
            self.assertIn("A,3500,4000,0,0,0,1.5,3,2,1,0.5,2,L", chart)
            manifest = (result / "level.ini").read_text(encoding="utf-8")
            self.assertIn("chart0_notes=3", manifest)
            self.assertIn("chart0_njs=18", manifest)
            self.assertIn("chart0_start_beat_offset=0.5", manifest)
            self.assertIn("chart0_obstacles=1", manifest)
            self.assertIn("chart0_arcs=1", manifest)
            report = json.loads((result / "conversion-report.json").read_text(
                encoding="utf-8"))
            self.assertEqual(report["bundleFormat"], 1)
            self.assertEqual(report["level"]["audioContainer"], "ogg")
            self.assertEqual(report["level"]["audioCodec"], "vorbis")
            self.assertEqual(report["charts"][0]["notes"], 3)
            self.assertRegex(report["source"]["contentSha1"], r"^[0-9a-f]{40}$")
            self.assertRegex(report["source"]["archiveSha1"], r"^[0-9a-f]{40}$")
            with zipfile.ZipFile(source) as raw_archive:
                archive = importer.SafeArchive(raw_archive)
                expected_content_hash = hashlib.sha1(
                    archive.read("Info.dat") + archive.read("Hard.dat")).hexdigest()
            self.assertEqual(report["source"]["contentSha1"], expected_content_hash)
            self.assertEqual(report["source"]["archiveSha1"],
                             hashlib.sha1(source.read_bytes()).hexdigest())
            listed = {item["path"]: item for item in report["files"]}
            self.assertIn("level.ini", listed)
            self.assertIn("audio.ogg", listed)
            self.assertIn("library.mblc", listed)
            self.assertEqual((result / "library.mblc").read_bytes()[:8],
                             b"MBLC\x02\x00\x00\x60")
            self.assertEqual(listed["audio.ogg"]["size"], len(OGG_FIXTURE))
            self.assertEqual(validate_bundle(result), [])
            (result / "audio.ogg").write_bytes(b"tampered")
            self.assertTrue(any("mismatch" in error for error in validate_bundle(result)))

    def test_inspect_archive_lists_metadata_without_extracting(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "inspect.zip"
            make_zip(source, {
                "Info.dat": {
                    "_version": "2.1.0", "_songName": "Inspect Me",
                    "_songAuthorName": "Artist", "_levelAuthorName": "Mapper",
                    "_songFilename": "song.ogg", "_beatsPerMinute": 128,
                    "_difficultyBeatmapSets": [{
                        "_beatmapCharacteristicName": "Standard",
                        "_difficultyBeatmaps": [{
                            "_difficulty": "Hard", "_beatmapFilename": "Hard.dat"}],
                    }],
                },
                "Hard.dat": {}, "song.ogg": OGG_FIXTURE,
            })
            inspection = importer.inspect_archive(source)
            self.assertEqual(inspection.title, "Inspect Me")
            self.assertEqual(inspection.song_author, "Artist")
            self.assertEqual(inspection.mapper, "Mapper")
            self.assertEqual(inspection.bpm, 128)
            self.assertEqual(inspection.difficulties[0].name, "Hard")

    def test_v3_bpm_events(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "v3.zip"
            make_zip(source, {
                "Info.dat": {
                    "_version": "2.1.0", "_songFilename": "song.ogg", "_beatsPerMinute": 120,
                    "_difficultyBeatmapSets": [{"_beatmapCharacteristicName": "Standard",
                        "_difficultyBeatmaps": [{"_difficulty": "Expert", "_beatmapFilename": "Expert.dat"}]}],
                },
                "Expert.dat": {"version": "3.2.0", "bpmEvents": [{"b": 2, "m": 60}],
                    "colorNotes": [{"b": 3, "x": 0, "y": 0, "c": 1,
                                    "d": 0, "a": -22.5}],
                    "bombNotes": [{"b": 4, "x": 2, "y": 1}],
                    "obstacles": [{"b": 1, "d": 2, "x": 0, "y": 2,
                                   "w": 1, "h": 3}],
                    "burstSliders": [{"b": 5, "tb": 6, "x": 0, "y": 0,
                                      "c": 0, "tx": 3, "ty": 2,
                                      "sc": 3, "s": 1.0}],
                    "sliders": [{"b": 6, "tb": 7, "x": 3, "y": 2,
                                 "d": 4, "mu": 1.25, "c": 1,
                                 "tx": 0, "ty": 0, "tc": 7, "tmu": 0.75,
                                 "m": 1}]},
                "song.ogg": OGG_FIXTURE,
            })
            result = import_archive(source, root / "out")
            chart = (result / "Standard_Expert.mbchart").read_text(encoding="utf-8")
            self.assertIn("2000,0,0,R,U,-22.5", chart)
            self.assertIn("3000,2,1,B,A", chart)
            self.assertIn("W,500,1500,0,1,2,1", chart)
            self.assertIn("C,4500,2,1,L", chart)
            self.assertIn("C,5000,3,2,L", chart)
            self.assertIn("A,5000,6000,3,2,4,1.25,0,0,7,0.75,1,R", chart)

    def test_v4_indexed_notes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "v4.zip"
            make_zip(source, {
                "Info.dat": {"version": "4.0.0", "song": {"title": "V4"},
                    "audio": {"songFilename": "song.ogg", "bpm": 100},
                    "difficultyBeatmaps": [{"characteristic": "Standard", "difficulty": "Easy",
                        "beatmapDataFilename": "Easy.dat"}]},
                "Easy.dat": {"version": "4.0.0", "colorNotes": [{"b": 1, "r": 0, "i": 0}],
                    "colorNotesData": [{"x": 3, "y": 2, "c": 0, "d": 5, "a": 45},
                                       {"x": 1, "y": 0, "c": 0, "d": 0}],
                    "bombNotes": [{"b": 2, "r": 0, "i": 0}],
                    "bombNotesData": [{"x": 1, "y": 0}],
                    "obstacles": [{"b": 3, "r": 0, "i": 0}],
                    "obstaclesData": [{"d": 1.5, "x": 2, "y": 0,
                                       "w": 2, "h": 5}],
                    "chains": [{"hb": 4, "tb": 5, "hr": 0, "tr": 0,
                                "i": 0, "ci": 0}],
                    "chainsData": [{"tx": 0, "ty": 0, "c": 3, "s": 1.0}],
                    "arcs": [{"hb": 6, "tb": 7, "hr": 0, "tr": 0,
                              "hi": 0, "ti": 1, "ai": 0}],
                    "arcsData": [{"m": 1.5, "tm": 0.5, "a": 2}]},
                "song.ogg": OGG_FIXTURE,
            })
            result = import_archive(source, root / "out")
            chart = (result / "Standard_Easy.mbchart").read_text(encoding="utf-8")
            self.assertIn("600,3,2,L,UR,45", chart)
            self.assertIn("1200,1,0,B,A", chart)
            self.assertIn("W,1800,900,2,2,0,3", chart)
            self.assertIn("C,2700,2,1,L", chart)
            self.assertIn("C,3000,0,0,L", chart)
            self.assertIn("A,3600,4200,3,2,5,1.5,1,0,0,0.5,2,L", chart)
            manifest = (result / "level.ini").read_text(encoding="utf-8")
            self.assertIn("chart0_chains=2", manifest)
            self.assertIn("chart0_arcs=1", manifest)

    def test_v4_content_hash_includes_bpm_beatmap_and_lightshow_in_order(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "v4-hash.zip"
            info = {"version": "4.0.0", "audio": {
                "songFilename": "song.ogg", "audioDataFilename": "BPMInfo.dat", "bpm": 100},
                "difficultyBeatmaps": [{"characteristic": "Standard", "difficulty": "Easy",
                    "beatmapDataFilename": "Easy.dat", "lightshowDataFilename": "Lights.dat"}]}
            make_zip(source, {"Info.dat": info, "BPMInfo.dat": b"bpm",
                              "Easy.dat": b"beatmap", "Lights.dat": b"lights",
                              "song.ogg": OGG_FIXTURE})
            with zipfile.ZipFile(source) as raw_archive:
                archive = importer.SafeArchive(raw_archive)
                expected = hashlib.sha1(archive.read("Info.dat") + b"bpmbeatmaplights").hexdigest()
                self.assertEqual(importer.beat_saber_content_sha1(archive, info), expected)

    def test_rejects_unsafe_archive_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "unsafe.zip"
            make_zip(source, {"../Info.dat": {}})
            with self.assertRaises(ImportError):
                import_archive(source, root / "out")

    def test_rejects_audio_that_ps3_cannot_decode(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "bad-audio.zip"
            make_zip(source, {
                "Info.dat": {
                    "_version": "2.1.0", "_songFilename": "song.egg",
                    "_beatsPerMinute": 120,
                    "_difficultyBeatmapSets": [{
                        "_beatmapCharacteristicName": "Standard",
                        "_difficultyBeatmaps": [{
                            "_difficulty": "Easy", "_beatmapFilename": "Easy.dat"}],
                    }],
                },
                "Easy.dat": {"_version": "2.0.0", "_notes": []},
                "song.egg": b"not actually an ogg stream",
            })
            with self.assertRaisesRegex(ImportError, "not Ogg/Vorbis"):
                import_archive(source, root / "out")

    def test_accepts_ogg_vorbis_payload_with_egg_filename_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "ogg-named-egg.zip"
            make_zip(source, {
                "Info.dat": {
                    "_version": "2.1.0", "_songFilename": "song.egg",
                    "_beatsPerMinute": 120,
                    "_difficultyBeatmapSets": [{
                        "_beatmapCharacteristicName": "Standard",
                        "_difficultyBeatmaps": [{
                            "_difficulty": "Easy", "_beatmapFilename": "Easy.dat"}],
                    }],
                },
                "Easy.dat": {"_version": "2.0.0", "_notes": []},
                "song.egg": OGG_FIXTURE,
            })
            result = import_archive(source, root / "out")
            self.assertEqual((result / "audio.egg").read_bytes(), OGG_FIXTURE)
            self.assertFalse((result / "audio.ogg").exists())
            report = json.loads((result / "conversion-report.json").read_text())
            self.assertEqual(report["level"]["audioContainer"], "ogg")
            self.assertEqual(report["level"]["audioCodec"], "vorbis")


if __name__ == "__main__":
    unittest.main()
