#!/usr/bin/env python3
"""Download or convert BeatSaver/Beat Saber maps into MoveBeat level bundles.

Network access is deliberately confined to this desktop-side tool. The PS3
only receives compact charts and the original media files.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import io
import json
import math
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import tempfile
from typing import Any, Callable, Iterable
import urllib.error
import urllib.parse
import urllib.request
import zipfile

# Support both `python -m tools.beatsaver_import` and the documented direct
# script invocation from the repository root.
if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.library_card import CARD_FILENAME, write_card
from tools.bundle_format import BUNDLE_FORMAT_VERSION, sha256_file


API_BASE = "https://api.beatsaver.com"
USER_AGENT = "Mozilla/5.0 (compatible; MoveBeat/0.1; PS3 homebrew level importer)"
MAX_DOWNLOAD_BYTES = 20 * 1024 * 1024
MAX_COVER_PREVIEW_BYTES = 4 * 1024 * 1024
MAX_MEMBER_BYTES = 64 * 1024 * 1024
MAX_TOTAL_BYTES = 128 * 1024 * 1024
CONVERTER_VERSION = "0.5.0"

DIRECTION_CODES = {
    0: "U",
    1: "D",
    2: "L",
    3: "R",
    4: "UL",
    5: "UR",
    6: "DL",
    7: "DR",
    8: "A",
}


class ImportError(ValueError):
    """A map is unavailable, unsafe, or unsupported."""


@dataclass(frozen=True)
class Difficulty:
    characteristic: str
    name: str
    filename: str
    note_jump_speed: float | None = None
    start_beat_offset: float = 0.0


@dataclass(frozen=True)
class ConvertedChart:
    difficulty: Difficulty
    filename: str
    note_count: int
    obstacle_count: int
    chain_count: int
    arc_count: int
    skipped_count: int


@dataclass(frozen=True)
class SourceMetadata:
    key: str = ""
    hash: str = ""
    archive_hash: str = ""
    mapper: str = ""
    download_url: str = ""


@dataclass(frozen=True)
class PreparedImport:
    """A validated bundle staged for explicit review before publication."""

    staging: Path
    destination: Path
    force: bool = False


@dataclass(frozen=True)
class SearchResult:
    key: str
    title: str
    mapper: str
    difficulties: tuple[str, ...]
    song_author: str = ""
    bpm: float | None = None
    rating_percent: float | None = None
    rating_count: int = 0
    upvotes: int = 0
    downvotes: int = 0
    downloads: int = 0
    plays: int = 0
    updated_at: str = ""
    cover_url: str = ""
    download_url: str = ""


@dataclass(frozen=True)
class ArchiveInspection:
    title: str
    song_author: str
    mapper: str
    bpm: float
    audio_filename: str
    cover_filename: str
    difficulties: tuple[Difficulty, ...]


class BeatTimeline:
    def __init__(self, initial_bpm: float, events: Iterable[tuple[float, float]], offset: float):
        if not math.isfinite(initial_bpm) or initial_bpm <= 0:
            raise ImportError("BPM must be a positive finite number")
        self.initial_bpm = initial_bpm
        self.offset = offset if math.isfinite(offset) else 0.0
        self.events = sorted(
            (float(beat), float(bpm))
            for beat, bpm in events
            if math.isfinite(float(beat)) and math.isfinite(float(bpm)) and float(bpm) > 0
        )

    def milliseconds(self, beat: float) -> int:
        beat = float(beat)
        if not math.isfinite(beat):
            raise ImportError("note beat must be finite")
        cursor = 0.0
        seconds = self.offset
        bpm = self.initial_bpm
        for event_beat, event_bpm in self.events:
            if event_beat <= cursor:
                bpm = event_bpm
                continue
            if event_beat >= beat:
                break
            seconds += (event_beat - cursor) * 60.0 / bpm
            cursor = event_beat
            bpm = event_bpm
        seconds += (beat - cursor) * 60.0 / bpm
        return max(0, int(round(seconds * 1000.0)))


class SafeArchive:
    def __init__(self, archive: zipfile.ZipFile):
        self.archive = archive
        self.members: dict[str, zipfile.ZipInfo] = {}
        total = 0
        for member in archive.infolist():
            if member.is_dir():
                continue
            if member.file_size > MAX_MEMBER_BYTES:
                raise ImportError(f"archive member is too large: {member.filename}")
            total += member.file_size
            if total > MAX_TOTAL_BYTES:
                raise ImportError("archive expands beyond the safety limit")
            path = PurePosixPath(member.filename.replace("\\", "/"))
            if path.is_absolute() or ".." in path.parts:
                raise ImportError(f"unsafe archive path: {member.filename}")
            key = path.name.casefold()
            if key in self.members:
                raise ImportError(f"ambiguous duplicate filename: {path.name}")
            self.members[key] = member

    def find(self, filename: str) -> zipfile.ZipInfo:
        key = PurePosixPath(filename.replace("\\", "/")).name.casefold()
        try:
            return self.members[key]
        except KeyError as exc:
            raise ImportError(f"archive does not contain {filename}") from exc

    def read(self, filename: str) -> bytes:
        return self.archive.read(self.find(filename))

    def read_json(self, filename: str) -> dict[str, Any]:
        raw = self.read(filename)
        if len(raw) > 8 * 1024 * 1024:
            raise ImportError(f"JSON file is too large: {filename}")
        try:
            value = json.loads(raw.decode("utf-8-sig"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise ImportError(f"invalid JSON in {filename}: {exc}") from exc
        if not isinstance(value, dict):
            raise ImportError(f"JSON root must be an object: {filename}")
        return value


def clean_text(value: Any) -> str:
    return str(value or "").replace("\r", " ").replace("\n", " ").replace("=", "-").strip()


def safe_name(value: str, fallback: str) -> str:
    result = re.sub(r"[^A-Za-z0-9._+-]+", "_", value).strip("._")
    return result[:80] or fallback


def info_member(archive: SafeArchive) -> str:
    for candidate in ("Info.dat", "info.dat"):
        try:
            return archive.find(candidate).filename
        except ImportError:
            pass
    raise ImportError("archive does not contain Info.dat")


def info_fields(info: dict[str, Any]) -> tuple[str, str, str, float, float, str, str]:
    if "_version" in info or "_difficultyBeatmapSets" in info:
        title = clean_text(info.get("_songName"))
        author = clean_text(info.get("_songAuthorName"))
        mapper = clean_text(info.get("_levelAuthorName"))
        bpm = float(info.get("_beatsPerMinute", 0))
        offset = float(info.get("_songTimeOffset", 0))
        audio = clean_text(info.get("_songFilename"))
        cover = clean_text(info.get("_coverImageFilename"))
    else:
        song = info.get("song") or {}
        audio_info = info.get("audio") or {}
        title = clean_text(song.get("title"))
        author = clean_text(song.get("author"))
        mapper = ""
        bpm = float(audio_info.get("bpm", 0))
        offset = 0.0
        audio = clean_text(audio_info.get("songFilename"))
        cover = clean_text(info.get("coverImageFilename"))
    if not audio:
        raise ImportError("Info.dat does not name an audio file")
    return title, author, mapper, bpm, offset, audio, cover


def difficulties(info: dict[str, Any]) -> list[Difficulty]:
    result: list[Difficulty] = []
    if "_difficultyBeatmapSets" in info:
        for beatmap_set in info.get("_difficultyBeatmapSets", []):
            characteristic = clean_text(beatmap_set.get("_beatmapCharacteristicName"))
            for item in beatmap_set.get("_difficultyBeatmaps", []):
                speed_key, offset_key = "_noteJumpMovementSpeed", "_noteJumpStartBeatOffset"
                result.append(_make_difficulty(
                    characteristic, clean_text(item.get("_difficulty")),
                    clean_text(item.get("_beatmapFilename")), item,
                    speed_key, offset_key))
    else:
        for item in info.get("difficultyBeatmaps", []):
            result.append(_make_difficulty(
                clean_text(item.get("characteristic")),
                clean_text(item.get("difficulty")),
                clean_text(item.get("beatmapDataFilename")), item,
                "noteJumpMovementSpeed", "noteJumpStartBeatOffset"))
    return [item for item in result if item.filename]


def _make_difficulty(characteristic: str, name: str, filename: str,
                     item: dict[str, Any], speed_key: str,
                     offset_key: str) -> Difficulty:
    raw_speed = item.get(speed_key)
    if raw_speed is None:
        return Difficulty(characteristic, name, filename)
    try:
        speed = float(raw_speed)
        offset = float(item.get(offset_key, 0.0))
    except (TypeError, ValueError, OverflowError) as exc:
        raise ImportError(f"invalid note jump metadata for {name}") from exc
    if not math.isfinite(speed) or not 0.1 <= speed <= 40.0:
        raise ImportError(f"note jump speed for {name} must be between 0.1 and 40")
    if not math.isfinite(offset) or not -16.0 <= offset <= 16.0:
        raise ImportError(f"note jump offset for {name} must be between -16 and 16")
    return Difficulty(characteristic, name, filename, speed, offset)


def bpm_events(beatmap: dict[str, Any]) -> list[tuple[float, float]]:
    if "bpmEvents" in beatmap:
        return [
            (float(event.get("b", 0)), float(event.get("m", 0)))
            for event in beatmap.get("bpmEvents", [])
        ]
    return [
        (float(event.get("_time", 0)), float(event.get("_floatValue", 0)))
        for event in beatmap.get("_events", [])
        if int(event.get("_type", -1)) == 100
    ]


def raw_notes(beatmap: dict[str, Any]) -> Iterable[tuple[float, int, int, int, int, float]]:
    if "_notes" in beatmap:
        for note in beatmap.get("_notes", []):
            note_type = int(note.get("_type", -1))
            if note_type not in (0, 1, 3):
                continue
            yield (
                float(note.get("_time", 0)),
                int(note.get("_lineIndex", -1)),
                int(note.get("_lineLayer", -1)),
                note_type,
                int(note.get("_cutDirection", 8)),
                float(note.get("_angleOffset", 0)),
            )
        return

    version = clean_text(beatmap.get("version"))
    color_notes = beatmap.get("colorNotes", [])
    if version.startswith("4") or "colorNotesData" in beatmap:
        data = beatmap.get("colorNotesData", [])
        for note in color_notes:
            index = int(note.get("i", -1))
            if index < 0 or index >= len(data):
                continue
            metadata = data[index]
            yield (
                float(note.get("b", 0)),
                int(metadata.get("x", -1)),
                int(metadata.get("y", -1)),
                int(metadata.get("c", -1)),
                int(metadata.get("d", 8)),
                float(metadata.get("a", 0)),
            )
    else:
        for note in color_notes:
            yield (
                float(note.get("b", 0)),
                int(note.get("x", -1)),
                int(note.get("y", -1)),
                int(note.get("c", -1)),
                int(note.get("d", 8)),
                float(note.get("a", 0)),
            )

    bomb_notes = beatmap.get("bombNotes", [])
    if version.startswith("4") or "bombNotesData" in beatmap:
        data = beatmap.get("bombNotesData", [])
        for note in bomb_notes:
            index = int(note.get("i", -1))
            if index < 0 or index >= len(data):
                continue
            metadata = data[index]
            yield (float(note.get("b", 0)), int(metadata.get("x", -1)),
                   int(metadata.get("y", -1)), 3, 8, 0.0)
    else:
        for note in bomb_notes:
            yield (float(note.get("b", 0)), int(note.get("x", -1)),
                   int(note.get("y", -1)), 3, 8, 0.0)


def raw_obstacles(beatmap: dict[str, Any]) -> Iterable[tuple[float, float, int, int, int, int]]:
    if "_obstacles" in beatmap:
        for obstacle in beatmap.get("_obstacles", []):
            obstacle_type = int(obstacle.get("_type", 0))
            if obstacle_type == 1:
                row, height = 2, 3
            elif obstacle_type == 2:
                row = int(obstacle.get("_lineLayer", -1))
                height = int(obstacle.get("_height", -1))
            else:
                row, height = 0, 5
            yield (float(obstacle.get("_time", 0)),
                   float(obstacle.get("_duration", 0)),
                   int(obstacle.get("_lineIndex", -1)),
                   int(obstacle.get("_width", -1)), row, height)
        return

    version = clean_text(beatmap.get("version"))
    obstacles = beatmap.get("obstacles", [])
    if version.startswith("4") or "obstaclesData" in beatmap:
        data = beatmap.get("obstaclesData", [])
        for obstacle in obstacles:
            index = int(obstacle.get("i", -1))
            if index < 0 or index >= len(data):
                continue
            metadata = data[index]
            yield (float(obstacle.get("b", 0)), float(metadata.get("d", 0)),
                   int(metadata.get("x", -1)), int(metadata.get("w", -1)),
                   int(metadata.get("y", -1)), int(metadata.get("h", -1)))
    else:
        for obstacle in obstacles:
            yield (float(obstacle.get("b", 0)), float(obstacle.get("d", 0)),
                   int(obstacle.get("x", -1)), int(obstacle.get("w", -1)),
                   int(obstacle.get("y", -1)), int(obstacle.get("h", -1)))


def raw_chains(beatmap: dict[str, Any]) -> Iterable[tuple[float, float, int, int, int, int, int, int, float]]:
    version = clean_text(beatmap.get("version"))
    if version.startswith("4") or "chainsData" in beatmap:
        notes_data = beatmap.get("colorNotesData", [])
        chains_data = beatmap.get("chainsData", [])
        for chain in beatmap.get("chains", []):
            note_index = int(chain.get("i", -1))
            chain_index = int(chain.get("ci", -1))
            if (note_index < 0 or note_index >= len(notes_data) or
                    chain_index < 0 or chain_index >= len(chains_data)):
                continue
            head = notes_data[note_index]
            tail = chains_data[chain_index]
            yield (float(chain.get("hb", 0)), float(chain.get("tb", 0)),
                   int(head.get("x", -1)), int(head.get("y", -1)),
                   int(head.get("c", -1)), int(tail.get("tx", -1)),
                   int(tail.get("ty", -1)), int(tail.get("c", -1)),
                   float(tail.get("s", 0)))
        return
    for chain in beatmap.get("burstSliders", []):
        yield (float(chain.get("b", 0)), float(chain.get("tb", 0)),
               int(chain.get("x", -1)), int(chain.get("y", -1)),
               int(chain.get("c", -1)), int(chain.get("tx", -1)),
               int(chain.get("ty", -1)), int(chain.get("sc", -1)),
               float(chain.get("s", 0)))


def raw_arcs(beatmap: dict[str, Any]) -> Iterable[
        tuple[float, float, int, int, int, float, int, int, int, float, int, int]]:
    if "_sliders" in beatmap:
        for arc in beatmap.get("_sliders", []):
            yield (float(arc.get("_headTime", 0)),
                   float(arc.get("_tailTime", 0)),
                   int(arc.get("_headLineIndex", -1)),
                   int(arc.get("_headLineLayer", -1)),
                   int(arc.get("_headCutDirection", 8)),
                   float(arc.get("_headControlPointLengthMultiplier", 1)),
                   int(arc.get("_tailLineIndex", -1)),
                   int(arc.get("_tailLineLayer", -1)),
                   int(arc.get("_tailCutDirection", 8)),
                   float(arc.get("_tailControlPointLengthMultiplier", 1)),
                   int(arc.get("_sliderMidAnchorMode", 0)),
                   int(arc.get("_colorType", -1)))
        return
    version = clean_text(beatmap.get("version"))
    if version.startswith("3"):
        for arc in beatmap.get("sliders", []):
            yield (float(arc.get("b", 0)), float(arc.get("tb", 0)),
                   int(arc.get("x", -1)), int(arc.get("y", -1)),
                   int(arc.get("d", 8)), float(arc.get("mu", 1)),
                   int(arc.get("tx", -1)), int(arc.get("ty", -1)),
                   int(arc.get("tc", 8)), float(arc.get("tmu", 1)),
                   int(arc.get("m", 0)), int(arc.get("c", -1)))
        return
    if version.startswith("4") or "arcsData" in beatmap:
        notes_data = beatmap.get("colorNotesData", [])
        arcs_data = beatmap.get("arcsData", [])
        for arc in beatmap.get("arcs", []):
            head_index = int(arc.get("hi", -1))
            tail_index = int(arc.get("ti", -1))
            arc_index = int(arc.get("ai", -1))
            if (head_index < 0 or head_index >= len(notes_data) or
                    tail_index < 0 or tail_index >= len(notes_data) or
                    arc_index < 0 or arc_index >= len(arcs_data)):
                continue
            head = notes_data[head_index]
            tail = notes_data[tail_index]
            metadata = arcs_data[arc_index]
            yield (float(arc.get("hb", 0)), float(arc.get("tb", 0)),
                   int(head.get("x", -1)), int(head.get("y", -1)),
                   int(head.get("d", 8)), float(metadata.get("m", 1)),
                   int(tail.get("x", -1)), int(tail.get("y", -1)),
                   int(tail.get("d", 8)), float(metadata.get("tm", 1)),
                   int(metadata.get("a", 0)), int(head.get("c", -1)))
        return
    for arc in beatmap.get("sliders", []):
        yield (float(arc.get("b", 0)), float(arc.get("tb", 0)),
               int(arc.get("x", -1)), int(arc.get("y", -1)),
               int(arc.get("d", 8)), float(arc.get("mu", 1)),
               int(arc.get("tx", -1)), int(arc.get("ty", -1)),
               int(arc.get("tc", 8)), float(arc.get("tmu", 1)),
               int(arc.get("m", 0)), int(arc.get("c", -1)))


def convert_chart(beatmap: dict[str, Any], difficulty: Difficulty, bpm: float,
                  offset: float) -> tuple[list[str], int, int, int, int, int]:
    timeline = BeatTimeline(bpm, bpm_events(beatmap), offset)
    converted: list[tuple[int, str]] = []
    note_count = 0
    obstacle_count = 0
    chain_count = 0
    arc_count = 0
    skipped = 0
    no_arrows = difficulty.characteristic.casefold() == "noarrows"
    for beat, lane, row, color, direction_code, angle_offset in raw_notes(beatmap):
        if (lane not in range(4) or row not in range(3) or
                color not in (0, 1, 3) or not math.isfinite(angle_offset)):
            skipped += 1
            continue
        direction = "A" if no_arrows or color == 3 else DIRECTION_CODES.get(direction_code)
        if direction is None:
            skipped += 1
            continue
        time_ms = timeline.milliseconds(beat)
        hand = "B" if color == 3 else ("L" if color == 0 else "R")
        angle_offset = ((angle_offset + 180.0) % 360.0) - 180.0
        if abs(angle_offset) < 0.0000005 or no_arrows or color == 3:
            line = f"{time_ms},{lane},{row},{hand},{direction}"
        else:
            angle_text = f"{angle_offset:.6f}".rstrip("0").rstrip(".")
            line = f"{time_ms},{lane},{row},{hand},{direction},{angle_text}"
        converted.append((time_ms, line))
        note_count += 1
    for beat, duration, lane, width, row, height in raw_obstacles(beatmap):
        if (not math.isfinite(beat) or not math.isfinite(duration) or duration <= 0 or
                lane not in range(4) or width < 1 or lane + width > 4 or
                row not in range(3) or height < 1):
            skipped += 1
            continue
        mapped_height = min(height, 3 - row)
        time_ms = timeline.milliseconds(beat)
        end_ms = timeline.milliseconds(beat + duration)
        duration_ms = end_ms - time_ms
        if duration_ms <= 0:
            skipped += 1
            continue
        converted.append((time_ms,
                          f"W,{time_ms},{duration_ms},{lane},{width},{row},{mapped_height}"))
        obstacle_count += 1
    for (head_beat, tail_beat, head_lane, head_row, color,
         tail_lane, tail_row, slices, squish) in raw_chains(beatmap):
        if (not math.isfinite(head_beat) or not math.isfinite(tail_beat) or
                not math.isfinite(squish) or tail_beat < head_beat or
                head_lane not in range(4) or head_row not in range(3) or
                color not in (0, 1) or tail_lane not in range(4) or
                tail_row not in range(3) or slices < 2 or slices > 64 or
                squish <= 0):
            skipped += 1
            continue
        hand = "L" if color == 0 else "R"
        for link in range(1, slices):
            time_fraction = link / (slices - 1)
            spatial_fraction = time_fraction * squish
            lane = int(math.floor(
                head_lane + (tail_lane - head_lane) * spatial_fraction + 0.5))
            row = int(math.floor(
                head_row + (tail_row - head_row) * spatial_fraction + 0.5))
            if lane not in range(4) or row not in range(3):
                skipped += 1
                continue
            beat = head_beat + (tail_beat - head_beat) * time_fraction
            time_ms = timeline.milliseconds(beat)
            converted.append((time_ms, f"C,{time_ms},{lane},{row},{hand}"))
            chain_count += 1
    for (head_beat, tail_beat, head_lane, head_row, head_direction,
         head_multiplier, tail_lane, tail_row, tail_direction,
         tail_multiplier, mid_anchor_mode, color) in raw_arcs(beatmap):
        if (not math.isfinite(head_beat) or not math.isfinite(tail_beat) or
                not math.isfinite(head_multiplier) or
                not math.isfinite(tail_multiplier) or
                tail_beat < head_beat or head_lane not in range(4) or
                tail_lane not in range(4) or head_row not in range(3) or
                tail_row not in range(3) or color not in (0, 1) or
                head_direction not in range(9) or tail_direction not in range(9) or
                head_multiplier < 0 or head_multiplier > 10 or
                tail_multiplier < 0 or tail_multiplier > 10 or
                mid_anchor_mode not in range(3)):
            skipped += 1
            continue
        head_ms = timeline.milliseconds(head_beat)
        tail_ms = timeline.milliseconds(tail_beat)
        if tail_ms < head_ms:
            skipped += 1
            continue
        hand = "L" if color == 0 else "R"
        converted.append((head_ms,
                          f"A,{head_ms},{tail_ms},{head_lane},{head_row},"
                          f"{head_direction},{head_multiplier:g},{tail_lane},"
                          f"{tail_row},{tail_direction},{tail_multiplier:g},"
                          f"{mid_anchor_mode},{hand}"))
        arc_count += 1
    converted.sort(key=lambda item: item[0])
    return ([item[1] for item in converted], note_count, obstacle_count,
            chain_count, arc_count, skipped)


def copy_member(archive: SafeArchive, source_name: str, destination: Path) -> None:
    destination.write_bytes(archive.read(source_name))


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def beat_saber_content_sha1(archive: SafeArchive,
                            info: dict[str, Any] | None = None) -> str:
    """Return the BeatSaver/SongCore identity hash, not the ZIP byte hash.

    BeatSaver hashes the normalized Info.dat bytes followed by referenced data
    files in Info.dat order. Version 4 additionally hashes its BPM data and
    separate lightshow data as the server validator reads them.
    """
    member = info_member(archive)
    raw_info = archive.read(member)
    if info is None:
        info = archive.read_json(member)
    digest = hashlib.sha1(raw_info)
    if "_difficultyBeatmapSets" in info:
        for item in difficulties(info):
            digest.update(archive.read(item.filename))
    else:
        audio = info.get("audio")
        if isinstance(audio, dict):
            bpm_filename = clean_text(audio.get("audioDataFilename"))
            if bpm_filename:
                digest.update(archive.read(bpm_filename))
        for item in info.get("difficultyBeatmaps", []):
            if not isinstance(item, dict):
                continue
            beatmap_filename = clean_text(item.get("beatmapDataFilename"))
            if beatmap_filename:
                digest.update(archive.read(beatmap_filename))
            lightshow_filename = clean_text(item.get("lightshowDataFilename"))
            if lightshow_filename:
                digest.update(archive.read(lightshow_filename))
    return digest.hexdigest()


def is_ogg_vorbis(data: bytes) -> bool:
    """Recognize the container/codec accepted by the PS3 ov_fopen path."""
    return data.startswith(b"OggS") and b"\x01vorbis" in data[:512]


def inspect_archive(zip_path: Path) -> ArchiveInspection:
    """Safely inspect a local Beat Saber ZIP without extracting it."""
    if not zip_path.is_file():
        raise ImportError(f"zip does not exist: {zip_path}")
    if zip_path.stat().st_size > MAX_DOWNLOAD_BYTES:
        raise ImportError("zip exceeds the 20 MiB BeatSaver import limit")
    if not zipfile.is_zipfile(zip_path):
        raise ImportError("file is not a ZIP archive")
    with zipfile.ZipFile(zip_path) as raw_archive:
        archive = SafeArchive(raw_archive)
        info = archive.read_json(info_member(archive))
        title, author, mapper, bpm, _offset, audio, cover = info_fields(info)
        available = tuple(difficulties(info))
        if not available:
            raise ImportError("archive does not contain any supported difficulties")
        return ArchiveInspection(title, author, mapper, bpm, audio, cover, available)


def write_manifest(path: Path, *, title: str, author: str, mapper: str, bpm: float,
                   audio: str, cover: str, source: SourceMetadata,
                   charts: list[ConvertedChart]) -> None:
    lines = [
        "format=1",
        "source=beatsaver" if source.key else "source=beat_saber_zip",
        f"source_key={clean_text(source.key)}",
        f"source_hash={clean_text(source.hash)}",
        f"title={clean_text(title)}",
        f"song_author={clean_text(author)}",
        f"mapper={clean_text(mapper or source.mapper)}",
        f"bpm={bpm:g}",
        f"audio={audio}",
        f"cover={cover}",
        f"chart_count={len(charts)}",
    ]
    for index, chart in enumerate(charts):
        lines.extend([
            f"chart{index}_characteristic={clean_text(chart.difficulty.characteristic)}",
            f"chart{index}_difficulty={clean_text(chart.difficulty.name)}",
            f"chart{index}_file={chart.filename}",
            f"chart{index}_notes={chart.note_count}",
            f"chart{index}_obstacles={chart.obstacle_count}",
            f"chart{index}_chains={chart.chain_count}",
            f"chart{index}_arcs={chart.arc_count}",
            f"chart{index}_skipped={chart.skipped_count}",
        ])
        if chart.difficulty.note_jump_speed is not None:
            lines.extend([
                f"chart{index}_njs={chart.difficulty.note_jump_speed:g}",
                f"chart{index}_start_beat_offset={chart.difficulty.start_beat_offset:g}",
            ])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_conversion_report(path: Path, *, title: str, author: str, mapper: str,
                            bpm: float, audio: str, cover: str,
                            source: SourceMetadata,
                            charts: list[ConvertedChart]) -> None:
    bundle_directory = path.parent
    files = []
    for item in sorted(bundle_directory.iterdir(), key=lambda value: value.name.casefold()):
        if not item.is_file() or item.name == path.name:
            continue
        files.append({
            "path": item.name,
            "size": item.stat().st_size,
            "sha256": sha256_file(item),
        })
    warnings = [
        f"{chart.difficulty.characteristic}/{chart.difficulty.name}: "
        f"skipped {chart.skipped_count} unsupported object(s)"
        for chart in charts if chart.skipped_count
    ]
    report = {
        "bundleFormat": BUNDLE_FORMAT_VERSION,
        "converter": {"name": "MoveBeat Track Assistant Core",
                      "version": CONVERTER_VERSION},
        "source": {
            "type": "beatsaver" if source.key else "beat_saber_zip",
            "key": clean_text(source.key),
            "contentSha1": clean_text(source.hash).lower(),
            "archiveSha1": clean_text(source.archive_hash).lower(),
        },
        "level": {
            "title": clean_text(title),
            "songAuthor": clean_text(author),
            "mapper": clean_text(mapper or source.mapper),
            "bpm": bpm,
            "audio": audio,
            "audioContainer": "ogg",
            "audioCodec": "vorbis",
            "cover": cover,
            "libraryCard": CARD_FILENAME,
        },
        "charts": [{
            "characteristic": chart.difficulty.characteristic,
            "difficulty": chart.difficulty.name,
            "file": chart.filename,
            "notes": chart.note_count,
            "obstacles": chart.obstacle_count,
            "chains": chart.chain_count,
            "arcs": chart.arc_count,
            "skipped": chart.skipped_count,
        } for chart in charts],
        "warnings": warnings,
        "files": files,
    }
    path.write_text(json.dumps(report, ensure_ascii=False, indent=2,
                               sort_keys=True) + "\n", encoding="utf-8")


def prepare_archive(zip_path: Path, output_root: Path, *,
                    characteristic: str | None = "Standard",
                    difficulty_name: str | None = None,
                    source: SourceMetadata = SourceMetadata(),
                    force: bool = False) -> PreparedImport:
    if zip_path.stat().st_size > MAX_DOWNLOAD_BYTES:
        raise ImportError("zip exceeds the 20 MiB BeatSaver import limit")
    archive_hash = sha1_file(zip_path)
    with zipfile.ZipFile(zip_path) as raw_archive:
        archive = SafeArchive(raw_archive)
        info = archive.read_json(info_member(archive))
        content_hash = beat_saber_content_sha1(archive, info)
        if source.hash and source.hash.casefold() != content_hash:
            raise ImportError("Beat Saber content SHA-1 does not match source metadata")
        effective_source = SourceMetadata(
            key=source.key, hash=content_hash,
            archive_hash=source.archive_hash or archive_hash,
            mapper=source.mapper, download_url=source.download_url)
        title, author, mapper, bpm, offset, audio_name, cover_name = info_fields(info)
        available = difficulties(info)
        selected = [
            item for item in available
            if (characteristic is None or item.characteristic.casefold() == characteristic.casefold())
            and (difficulty_name is None or item.name.casefold() == difficulty_name.casefold())
        ]
        if not selected:
            choices = ", ".join(f"{item.characteristic}/{item.name}" for item in available)
            raise ImportError(f"requested difficulty is unavailable; archive contains: {choices}")

        identity = effective_source.key or effective_source.hash[:12] or zip_path.stem
        folder_name = safe_name(identity, "imported_map")
        destination = output_root / folder_name
        if destination.exists() and not force:
            raise ImportError(f"destination already exists: {destination} (use --force)")

        output_root.mkdir(parents=True, exist_ok=True)
        staging = Path(tempfile.mkdtemp(prefix=f".{folder_name}-", dir=output_root))
        charts: list[ConvertedChart] = []
        try:
            audio_data = archive.read(audio_name)
            if not is_ogg_vorbis(audio_data):
                raise ImportError(
                    "audio is not Ogg/Vorbis; transcode it before PS3 export")
            audio_extension = Path(audio_name).suffix.lower() or ".ogg"
            output_audio = "audio" + audio_extension
            (staging / output_audio).write_bytes(audio_data)
            output_cover = ""
            if cover_name:
                cover_extension = Path(cover_name).suffix.lower() or ".png"
                output_cover = "cover" + cover_extension
                copy_member(archive, cover_name, staging / output_cover)

            try:
                write_card(staging, output_cover, title, author, mapper)
            except ValueError as exc:
                raise ImportError(str(exc)) from exc

            for item in selected:
                beatmap = archive.read_json(item.filename)
                (rows, note_count, obstacle_count, chain_count,
                 arc_count, skipped) = convert_chart(beatmap, item, bpm, offset)
                chart_name = safe_name(f"{item.characteristic}_{item.name}", "chart") + ".mbchart"
                (staging / chart_name).write_text(
                    "# notes: time_ms,lane,row,hand,direction\n"
                    "# walls: W,time_ms,duration_ms,lane,width,row,height\n" +
                    "# chains: C,time_ms,lane,row,hand\n" +
                    "# arcs: A,head_ms,tail_ms,head_lane,head_row,head_dir,head_mu,"
                    "tail_lane,tail_row,tail_dir,tail_mu,mid_mode,hand\n" +
                    "\n".join(rows) + "\n",
                    encoding="utf-8",
                )
                charts.append(ConvertedChart(
                    item, chart_name, note_count, obstacle_count, chain_count,
                    arc_count, skipped))

            write_manifest(
                staging / "level.ini", title=title, author=author, mapper=mapper,
                bpm=bpm, audio=output_audio, cover=output_cover,
                source=effective_source, charts=charts,
            )
            write_conversion_report(
                staging / "conversion-report.json", title=title, author=author,
                mapper=mapper, bpm=bpm, audio=output_audio, cover=output_cover,
                source=effective_source, charts=charts,
            )
            return PreparedImport(staging, destination, force)
        except Exception:
            shutil.rmtree(staging, ignore_errors=True)
            raise


def commit_prepared_import(prepared: PreparedImport) -> Path:
    """Publish a reviewed staging directory under its final bundle name."""
    if not prepared.staging.is_dir():
        raise ImportError("staged bundle no longer exists")
    if prepared.destination.exists():
        if not prepared.force:
            raise ImportError(f"destination already exists: {prepared.destination}")
        shutil.rmtree(prepared.destination)
    prepared.staging.rename(prepared.destination)
    return prepared.destination


def discard_prepared_import(prepared: PreparedImport) -> None:
    """Remove a staged bundle when the user rejects its conversion preview."""
    if prepared.staging.exists():
        shutil.rmtree(prepared.staging)


def import_archive(zip_path: Path, output_root: Path, *,
                   characteristic: str | None = "Standard",
                   difficulty_name: str | None = None,
                   source: SourceMetadata = SourceMetadata(), force: bool = False) -> Path:
    prepared = prepare_archive(
        zip_path, output_root, characteristic=characteristic,
        difficulty_name=difficulty_name, source=source, force=force)
    try:
        return commit_prepared_import(prepared)
    except Exception:
        discard_prepared_import(prepared)
        raise


def request_json(url: str) -> dict[str, Any]:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Accept": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            raw = response.read(2 * 1024 * 1024 + 1)
    except urllib.error.URLError as exc:
        raise ImportError(f"BeatSaver API request failed: {exc}") from exc
    if len(raw) > 2 * 1024 * 1024:
        raise ImportError("BeatSaver API response is unexpectedly large")
    try:
        value = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ImportError("BeatSaver API returned invalid JSON") from exc
    if not isinstance(value, dict):
        raise ImportError("BeatSaver API returned an unexpected response")
    return value


def select_version(versions: object, want_hash: str | None = None) -> dict[str, Any]:
    """Choose the newest downloadable published version without trusting API order.

    With `want_hash`, the downloadable version with that content hash wins
    (a playlist names one exact version); otherwise the newest is used.
    """
    if not isinstance(versions, list):
        raise ImportError("BeatSaver returned an invalid versions list")

    usable = [
        version for version in versions
        if isinstance(version, dict) and clean_text(version.get("downloadURL"))
    ]
    if not usable:
        raise ImportError("BeatSaver map has no downloadable versions")
    if want_hash:
        for version in usable:
            if clean_text(version.get("hash")).casefold() == want_hash.casefold():
                return version

    published = [
        version for version in usable
        if clean_text(version.get("state")).casefold() == "published"
    ]
    candidates = published or usable

    def created_at(version: dict[str, Any]) -> float:
        value = clean_text(version.get("createdAt"))
        if not value:
            return float("-inf")
        try:
            parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
            if parsed.tzinfo is None:
                parsed = parsed.replace(tzinfo=timezone.utc)
            return parsed.timestamp()
        except (ValueError, OverflowError):
            return float("-inf")

    # max() is stable, so an API response with no usable timestamps retains
    # its original preference instead of relying on a guessed version number.
    return max(candidates, key=created_at)


def search_maps(query: str, limit: int = 10) -> list[SearchResult]:
    query = query.strip()
    if not query:
        raise ImportError("BeatSaver search query cannot be empty")
    if not 1 <= limit <= 100:
        raise ImportError("BeatSaver search limit must be between 1 and 100")

    bsr_match = re.fullmatch(r"!bsr\s+([0-9a-f]+)", query, re.IGNORECASE)
    plain_key = re.fullmatch(r"[0-9a-f]+", query, re.IGNORECASE)
    if bsr_match or (plain_key and any(character.isdigit() for character in query)):
        key = bsr_match.group(1) if bsr_match else plain_key.group(0)
        item = request_json(f"{API_BASE}/maps/id/{urllib.parse.quote(key, safe='')}")
        result = search_result_from_map(item)
        if result is None:
            raise ImportError(f"BeatSaver returned invalid metadata for map {key}")
        return [result]

    parameters = urllib.parse.urlencode({
        "q": query,
        "order": "Relevance",
        "pageSize": limit,
    })
    response = request_json(f"{API_BASE}/search/text/0?{parameters}")
    docs = response.get("docs")
    if not isinstance(docs, list):
        raise ImportError("BeatSaver search returned an unexpected response")

    results: list[SearchResult] = []
    for item in docs[:limit]:
        result = search_result_from_map(item)
        if result is not None:
            results.append(result)
    return results


def search_result_from_map(item: object) -> SearchResult | None:
    if not isinstance(item, dict):
        return None
    key = clean_text(item.get("id"))
    if not key:
        return None
    metadata = item.get("metadata") if isinstance(item.get("metadata"), dict) else {}
    uploader = item.get("uploader") if isinstance(item.get("uploader"), dict) else {}
    stats = item.get("stats") if isinstance(item.get("stats"), dict) else {}
    difficulties: list[str] = []
    try:
        version = select_version(item.get("versions"))
    except ImportError:
        version = {}
    diffs = version.get("diffs") if isinstance(version.get("diffs"), list) else []
    for diff in diffs:
        if not isinstance(diff, dict):
            continue
        characteristic = clean_text(diff.get("characteristic"))
        difficulty = clean_text(diff.get("difficulty"))
        label = f"{characteristic}:{difficulty}" if characteristic and difficulty else difficulty
        if label and label not in difficulties:
            difficulties.append(label)
    raw_bpm = metadata.get("bpm")
    try:
        bpm = float(raw_bpm)
        if not math.isfinite(bpm) or bpm <= 0:
            bpm = None
    except (TypeError, ValueError, OverflowError):
        bpm = None
    raw_rating = stats.get("score", stats.get("scoreOneDP"))
    try:
        rating_percent = float(raw_rating)
        if not math.isfinite(rating_percent) or rating_percent < 0:
            rating_percent = None
        elif rating_percent <= 1.0:
            rating_percent *= 100.0
        elif rating_percent > 100.0:
            rating_percent = None
    except (TypeError, ValueError, OverflowError):
        rating_percent = None
    def nonnegative_count(field: str) -> int:
        try:
            return max(0, int(stats.get(field, 0)))
        except (TypeError, ValueError, OverflowError):
            return 0

    upvotes = nonnegative_count("upvotes")
    downvotes = nonnegative_count("downvotes")
    rating_count = upvotes + downvotes or nonnegative_count("ratings")
    downloads = nonnegative_count("downloads")
    plays = nonnegative_count("plays")
    return SearchResult(
        key=key,
        title=clean_text(item.get("name") or metadata.get("songName")) or "Untitled",
        mapper=clean_text(uploader.get("name") or metadata.get("levelAuthorName")) or "unknown",
        difficulties=tuple(difficulties),
        song_author=clean_text(metadata.get("songAuthorName")),
        bpm=bpm,
        rating_percent=rating_percent,
        rating_count=rating_count,
        upvotes=upvotes,
        downvotes=downvotes,
        downloads=downloads,
        plays=plays,
        updated_at=clean_text(version.get("createdAt")),
        cover_url=clean_text(version.get("coverURL")),
        download_url=clean_text(version.get("downloadURL")),
    )


def download_archive_size(url: str) -> int | None:
    """Return a server-advertised ZIP size without downloading its body."""
    parsed = urllib.parse.urlparse(url)
    host = (parsed.hostname or "").casefold()
    if parsed.scheme != "https" or not (
            host == "beatsaver.com" or host.endswith(".beatsaver.com")):
        raise ImportError("BeatSaver returned a download URL outside its HTTPS domains")
    for method in ("HEAD", "GET"):
        headers = {"User-Agent": USER_AGENT}
        if method == "GET":
            # Some object stores do not implement HEAD consistently. A one-
            # byte range gives Content-Range without reading the ZIP body.
            headers["Range"] = "bytes=0-0"
        request = urllib.request.Request(url, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                final = urllib.parse.urlparse(response.geturl())
                final_host = (final.hostname or "").casefold()
                if final.scheme != "https" or not (
                        final_host == "beatsaver.com" or
                        final_host.endswith(".beatsaver.com")):
                    raise ImportError(
                        "BeatSaver download redirected outside its HTTPS domains")
                raw_size = ""
                if method == "GET":
                    content_range = response.headers.get("Content-Range", "")
                    match = re.fullmatch(r"bytes \d+-\d+/(\d+)", content_range)
                    if match:
                        raw_size = match.group(1)
                if not raw_size:
                    raw_size = response.headers.get("Content-Length", "")
                if raw_size.isdigit():
                    return int(raw_size)
        except urllib.error.HTTPError as exc:
            exc.close()
            if method == "HEAD" and exc.code in (403, 404, 405, 501):
                continue
            if exc.code in (403, 404, 405, 416, 501):
                return None
            raise ImportError(f"BeatSaver ZIP size check failed: HTTP {exc.code}") from exc
        except urllib.error.URLError as exc:
            raise ImportError(f"BeatSaver ZIP size check failed: {exc}") from exc
    return None


def download_cover_preview(url: str) -> bytes:
    """Download one small HTTPS cover from BeatSaver's own image hosts."""
    parsed = urllib.parse.urlparse(url)
    host = (parsed.hostname or "").casefold()
    if parsed.scheme != "https" or not (
            host == "beatsaver.com" or host.endswith(".beatsaver.com")):
        raise ImportError("BeatSaver returned a cover URL outside its HTTPS domains")
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            final = urllib.parse.urlparse(response.geturl())
            final_host = (final.hostname or "").casefold()
            if final.scheme != "https" or not (
                    final_host == "beatsaver.com" or
                    final_host.endswith(".beatsaver.com")):
                raise ImportError("BeatSaver cover redirected outside its HTTPS domains")
            raw_length = response.headers.get("Content-Length")
            if raw_length and raw_length.isdigit() and int(raw_length) > MAX_COVER_PREVIEW_BYTES:
                raise ImportError("cover preview exceeds 4 MiB")
            data = response.read(MAX_COVER_PREVIEW_BYTES + 1)
    except urllib.error.URLError as exc:
        raise ImportError(f"BeatSaver cover preview failed: {exc}") from exc
    if len(data) > MAX_COVER_PREVIEW_BYTES:
        raise ImportError("cover preview exceeds 4 MiB")
    return data


def read_archive_cover_preview(zip_path: Path) -> bytes:
    """Read a bounded cover image from a validated local map archive."""
    with zipfile.ZipFile(zip_path) as raw_archive:
        archive = SafeArchive(raw_archive)
        info = archive.read_json(info_member(archive))
        cover_name = info_fields(info)[-1]
        if not cover_name:
            return b""
        member = archive.find(cover_name)
        if member.file_size > MAX_COVER_PREVIEW_BYTES:
            raise ImportError("cover preview exceeds 4 MiB")
        return archive.read(cover_name)


def download_map(key: str, destination: Path, *,
                 progress: Callable[[int, int | None], None] | None = None,
                 cancelled: Callable[[], bool] | None = None,
                 want_hash: str | None = None) -> SourceMetadata:
    safe_key = urllib.parse.quote(key, safe="")
    metadata = request_json(f"{API_BASE}/maps/id/{safe_key}")
    try:
        version = select_version(metadata.get("versions"), want_hash)
    except ImportError as exc:
        raise ImportError(f"BeatSaver map {key} has no downloadable versions") from exc
    download_url = clean_text(version.get("downloadURL"))
    parsed = urllib.parse.urlparse(download_url)
    if parsed.scheme != "https" or not (
        parsed.hostname == "beatsaver.com" or (parsed.hostname or "").endswith(".beatsaver.com")
    ):
        raise ImportError("BeatSaver returned a download URL outside its HTTPS domains")

    request = urllib.request.Request(download_url, headers={"User-Agent": USER_AGENT})
    size = 0
    try:
        with urllib.request.urlopen(request, timeout=60) as response, destination.open("wb") as output:
            raw_total = response.headers.get("Content-Length")
            total = int(raw_total) if raw_total and raw_total.isdigit() else None
            while True:
                if cancelled is not None and cancelled():
                    raise ImportError("BeatSaver download was cancelled")
                chunk = response.read(64 * 1024)
                if not chunk:
                    break
                size += len(chunk)
                if size > MAX_DOWNLOAD_BYTES:
                    raise ImportError("BeatSaver download exceeds 20 MiB")
                output.write(chunk)
                if progress is not None:
                    progress(size, total)
    except urllib.error.URLError as exc:
        destination.unlink(missing_ok=True)
        raise ImportError(f"BeatSaver download failed: {exc}") from exc
    except Exception:
        destination.unlink(missing_ok=True)
        raise

    if not zipfile.is_zipfile(destination):
        destination.unlink(missing_ok=True)
        raise ImportError("BeatSaver download is not a ZIP archive")
    expected_hash = clean_text(version.get("hash")).lower()
    try:
        with zipfile.ZipFile(destination) as raw_archive:
            archive = SafeArchive(raw_archive)
            actual_hash = beat_saber_content_sha1(archive)
    except (OSError, zipfile.BadZipFile, ImportError):
        destination.unlink(missing_ok=True)
        raise
    if re.fullmatch(r"[0-9a-f]{40}", expected_hash) and actual_hash != expected_hash:
        destination.unlink(missing_ok=True)
        raise ImportError("Beat Saber content SHA-1 does not match BeatSaver metadata")
    uploader = metadata.get("uploader") or {}
    return SourceMetadata(
        key=clean_text(metadata.get("id") or key),
        hash=actual_hash,
        archive_hash=sha1_file(destination),
        mapper=clean_text(uploader.get("name")),
        download_url=download_url,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument("--key", help="BeatSaver map key/id to download")
    source_group.add_argument("--zip", type=Path, help="already downloaded BeatSaver/Beat Saber ZIP")
    source_group.add_argument("--search", help="search BeatSaver and print map keys")
    parser.add_argument("--limit", type=int, default=10, help="search result count, 1..100 (default: 10)")
    parser.add_argument("--output", type=Path, default=Path("out/levels"))
    parser.add_argument("--characteristic", default="Standard", help="default: Standard; use '*' for all")
    parser.add_argument("--difficulty", help="Easy, Normal, Hard, Expert, or ExpertPlus")
    parser.add_argument("--force", action="store_true", help="replace an existing imported bundle")
    args = parser.parse_args()

    characteristic = None if args.characteristic == "*" else args.characteristic
    temporary: Path | None = None
    try:
        if args.search is not None:
            results = search_maps(args.search, args.limit)
            for result in results:
                difficulties = ", ".join(result.difficulties) or "difficulty data unavailable"
                print(f"{result.key}\t{result.title} — {result.mapper} [{difficulties}]")
            return 0
        source = SourceMetadata()
        zip_path = args.zip
        if args.key:
            handle = tempfile.NamedTemporaryFile(prefix="movebeat-", suffix=".zip", delete=False)
            handle.close()
            temporary = Path(handle.name)
            source = download_map(args.key, temporary)
            zip_path = temporary
        assert zip_path is not None
        destination = import_archive(
            zip_path, args.output, characteristic=characteristic,
            difficulty_name=args.difficulty, source=source, force=args.force,
        )
        print(f"Imported level bundle: {destination}")
        print("Use downloaded content only as permitted by its rightsholders and BeatSaver's terms.")
        return 0
    except (OSError, zipfile.BadZipFile, ImportError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
