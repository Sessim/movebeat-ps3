#!/usr/bin/env python3
"""Import a BeatSaver playlist (.bplist) as a MoveBeat playlist bundle.

The source is a local .bplist file, a BeatSaver playlist page URL
(https://beatsaver.com/playlists/<id>) or its numeric id. Every map the
playlist names is imported as an ordinary level bundle unless the library
already has it (same content hash, or same BeatSaver key); then a playlist
bundle is written next to the levels:

    <output>/playlist-<name>/playlist.ini      title, author, songs in order
                             library.mblc      card shown on the PS3 shelf
                             cover.png         the playlist image, if any
                             conversion-report.json

The PS3 matches playlist songs to level bundles by content hash, so a song
whose BeatSaver map has a newer version than the one the playlist names
still finds the bundle that was actually imported.

    python tools/playlist_import.py https://beatsaver.com/playlists/12345
    python tools/playlist_import.py my_maps.bplist --no-download
"""

from __future__ import annotations

import argparse
import base64
import binascii
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
from typing import Any, Callable
import urllib.error
import urllib.parse
import urllib.request

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.beatsaver_import import (  # noqa: E402
    API_BASE, USER_AGENT, ImportError, clean_text, download_map, import_archive,
    request_json, safe_name,
)
from tools.bundle_format import BUNDLE_FORMAT_VERSION, CARD_FILENAME, sha256_file  # noqa: E402
from tools.library_card import render_card_bytes  # noqa: E402


MAX_PLAYLIST_BYTES = 8 * 1024 * 1024
MAX_PLAYLIST_SONGS = 256
MAX_IMAGE_BYTES = 4 * 1024 * 1024
HASH_PATTERN = re.compile(r"[0-9a-fA-F]{40}")
PLAYLIST_URL = re.compile(r"https://(?:www\.)?beatsaver\.com/playlists/(\d{1,10})/?(?:[?#].*)?")
DIFFICULTY_NAMES = {
    "easy": "Easy", "normal": "Normal", "hard": "Hard",
    "expert": "Expert", "expertplus": "ExpertPlus",
}


@dataclass
class PlaylistEntry:
    hash: str = ""
    key: str = ""
    title: str = ""
    characteristic: str = ""
    difficulty: str = ""
    # Set by resolve_entries when the library has the map.
    available: bool = False


@dataclass
class Playlist:
    title: str
    author: str = ""
    image: bytes | None = None
    entries: list[PlaylistEntry] = field(default_factory=list)
    source_id: str = ""


@dataclass
class PlaylistImportReport:
    imported: list[str] = field(default_factory=list)
    existing: list[str] = field(default_factory=list)
    failed: list[tuple[str, str]] = field(default_factory=list)


def _difficulty(value: Any) -> str:
    folded = re.sub(r"[^a-z]", "", str(value or "").casefold())
    return DIFFICULTY_NAMES.get(folded, "")


def _image(value: Any) -> bytes | None:
    """The playlist image: a base64 PNG/JPEG, optionally as a data URI."""
    text = str(value or "").strip()
    if not text:
        return None
    if text.startswith("data:"):
        text = text.split(",", 1)[-1]
    if len(text) > MAX_IMAGE_BYTES * 4 // 3 + 4:
        return None
    try:
        data = base64.b64decode(text, validate=True)
    except (binascii.Error, ValueError):
        return None
    if data.startswith(b"\x89PNG\r\n\x1a\n") or data.startswith(b"\xff\xd8\xff"):
        return data
    return None


def parse_bplist(source: bytes | str | dict[str, Any], source_id: str = "") -> Playlist:
    """Validate a .bplist document and keep what MoveBeat uses."""
    if isinstance(source, (bytes, str)):
        if len(source) > MAX_PLAYLIST_BYTES:
            raise ImportError("playlist exceeds 8 MiB")
        try:
            source = json.loads(source)
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            raise ImportError(f"playlist is not valid JSON: {exc}") from exc
    if not isinstance(source, dict):
        raise ImportError("playlist root must be an object")
    title = clean_text(source.get("playlistTitle"))
    if not title:
        raise ImportError("playlist has no playlistTitle")
    songs = source.get("songs")
    if not isinstance(songs, list) or not songs:
        raise ImportError("playlist has no songs")
    if len(songs) > MAX_PLAYLIST_SONGS:
        raise ImportError(f"playlist has more than {MAX_PLAYLIST_SONGS} songs")
    playlist = Playlist(title=title, author=clean_text(source.get("playlistAuthor")),
                        image=_image(source.get("image")), source_id=source_id)
    for index, song in enumerate(songs):
        if not isinstance(song, dict):
            raise ImportError(f"songs[{index}] must be an object")
        entry = PlaylistEntry(title=clean_text(song.get("songName")))
        song_hash = clean_text(song.get("hash"))
        if song_hash:
            if not HASH_PATTERN.fullmatch(song_hash):
                raise ImportError(f"songs[{index}].hash is not a SHA-1")
            entry.hash = song_hash.casefold()
        key = clean_text(song.get("key"))
        if key and re.fullmatch(r"[0-9A-Za-z]{1,16}", key):
            entry.key = key.casefold()
        if not entry.hash and not entry.key:
            raise ImportError(f"songs[{index}] needs a hash or a key")
        difficulties = song.get("difficulties")
        if isinstance(difficulties, list) and difficulties and isinstance(difficulties[0], dict):
            entry.difficulty = _difficulty(difficulties[0].get("name"))
            if entry.difficulty:
                entry.characteristic = clean_text(difficulties[0].get("characteristic")) or "Standard"
        playlist.entries.append(entry)
    return playlist


def playlist_id(source: str) -> str | None:
    """The BeatSaver playlist id of a page URL or a bare number."""
    source = source.strip()
    if re.fullmatch(r"\d{1,10}", source):
        return source
    match = PLAYLIST_URL.fullmatch(source)
    return match.group(1) if match else None


def download_bplist(identifier: str) -> bytes:
    if not re.fullmatch(r"\d{1,10}", identifier):
        raise ImportError("BeatSaver playlist id must be a number")
    url = f"{API_BASE}/playlists/id/{identifier}/download"
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read(MAX_PLAYLIST_BYTES + 1)
    except urllib.error.URLError as exc:
        raise ImportError(f"BeatSaver playlist download failed: {exc}") from exc
    if len(data) > MAX_PLAYLIST_BYTES:
        raise ImportError("BeatSaver playlist exceeds 8 MiB")
    return data


def load_playlist(source: str) -> Playlist:
    path = Path(source)
    if path.is_file():
        if path.stat().st_size > MAX_PLAYLIST_BYTES:
            raise ImportError("playlist exceeds 8 MiB")
        return parse_bplist(path.read_bytes(), source_id=path.stem)
    identifier = playlist_id(source)
    if identifier is None:
        raise ImportError("give a .bplist file, a beatsaver.com/playlists/<id> URL or an id")
    return parse_bplist(download_bplist(identifier), source_id=identifier)


def _manifest(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if "=" in line and not line.lstrip().startswith("#"):
                name, value = line.split("=", 1)
                values[name.strip()] = value.strip()
    except (OSError, UnicodeDecodeError):
        pass
    return values


def library_index(output_root: Path) -> tuple[dict[str, str], dict[str, tuple[str, str]]]:
    """Bundles in `output_root`: by content hash, and by key -> (hash, name)."""
    by_hash: dict[str, str] = {}
    by_key: dict[str, tuple[str, str]] = {}
    if not output_root.is_dir():
        return by_hash, by_key
    for directory in sorted(output_root.iterdir()):
        level = directory / "level.ini"
        if directory.name.startswith(".") or not level.is_file():
            continue
        values = _manifest(level)
        content_hash = values.get("source_hash", "").casefold()
        if HASH_PATTERN.fullmatch(content_hash):
            by_hash.setdefault(content_hash, directory.name)
        key = values.get("source_key", "").casefold()
        if key:
            by_key.setdefault(key, (content_hash, directory.name))
    return by_hash, by_key


def _key_for_hash(content_hash: str) -> str:
    metadata = request_json(f"{API_BASE}/maps/hash/{urllib.parse.quote(content_hash)}")
    key = clean_text(metadata.get("id"))
    if not re.fullmatch(r"[0-9A-Za-z]{1,16}", key):
        raise ImportError("BeatSaver returned no map id for this hash")
    return key


def fetch_map(entry: PlaylistEntry, output_root: Path) -> Path:
    """Download one playlist map from BeatSaver and import it."""
    key = entry.key or _key_for_hash(entry.hash)
    handle = tempfile.NamedTemporaryFile(prefix="movebeat-", suffix=".zip", delete=False)
    handle.close()
    temporary = Path(handle.name)
    try:
        source = download_map(key, temporary, want_hash=entry.hash or None)
        # All charts: the playlist or the player picks the difficulty.
        return import_archive(temporary, output_root, characteristic=None, source=source)
    finally:
        temporary.unlink(missing_ok=True)


def resolve_entries(playlist: Playlist, output_root: Path, *, download: bool = True,
                    fetch: Callable[[PlaylistEntry, Path], Path] = fetch_map,
                    log: Callable[[str], None] = print) -> PlaylistImportReport:
    """Make sure every song is in the library; point entries at the bundles.

    An entry whose map is found by key but with another content hash (a
    different version of the map) is rewritten to the hash in the library,
    so the PS3 finds the version the player actually has.
    """
    report = PlaylistImportReport()
    for index, entry in enumerate(playlist.entries):
        label = entry.title or entry.key or entry.hash[:12]
        by_hash, by_key = library_index(output_root)
        if entry.hash and entry.hash in by_hash:
            report.existing.append(by_hash[entry.hash])
            entry.available = True
            continue
        if entry.key and entry.key in by_key:
            local_hash, name = by_key[entry.key]
            if local_hash and local_hash != entry.hash:
                if entry.hash:
                    log(f"{label}: the library has another version of {entry.key}; using it")
                entry.hash = local_hash
            report.existing.append(name)
            entry.available = True
            continue
        if not download:
            report.failed.append((label, "not in the library (download disabled)"))
            continue
        try:
            log(f"[{index + 1}/{len(playlist.entries)}] downloading {label}")
            bundle = fetch(entry, output_root)
        except (OSError, ImportError) as exc:
            report.failed.append((label, str(exc)))
            log(f"{label}: {exc}")
            continue
        values = _manifest(bundle / "level.ini")
        local_hash = values.get("source_hash", "").casefold()
        if HASH_PATTERN.fullmatch(local_hash):
            entry.hash = local_hash
        entry.key = entry.key or values.get("source_key", "").casefold()
        report.imported.append(bundle.name)
        entry.available = True
    return report


def _ini_value(value: str) -> str:
    return clean_text(value)


def write_playlist_bundle(playlist: Playlist, output_root: Path, *,
                          force: bool = False) -> Path:
    """Write the playlist bundle for the entries the library has."""
    entries = [entry for entry in playlist.entries if entry.available]
    if not entries:
        raise ImportError("none of the playlist's maps is in the library")
    name = "playlist-" + safe_name(playlist.source_id or playlist.title, "list")
    destination = output_root / name
    if destination.exists() and not force:
        raise ImportError(f"destination already exists: {destination} (use --force)")
    output_root.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".{name}-", dir=output_root))
    try:
        cover_name = ""
        if playlist.image:
            cover_name = "cover.png" if playlist.image.startswith(b"\x89PNG") else "cover.jpg"
            (staging / cover_name).write_bytes(playlist.image)
        lines = ["format=1", f"title={_ini_value(playlist.title)}",
                 f"author={_ini_value(playlist.author)}",
                 f"song_count={len(entries)}"]
        for index, entry in enumerate(entries):
            prefix = f"song{index}_"
            if entry.hash:
                lines.append(f"{prefix}hash={entry.hash}")
            if entry.key:
                lines.append(f"{prefix}key={entry.key}")
            if entry.title:
                lines.append(f"{prefix}title={_ini_value(entry.title)}")
            if entry.difficulty:
                lines.append(f"{prefix}characteristic={_ini_value(entry.characteristic or 'Standard')}")
                lines.append(f"{prefix}difficulty={entry.difficulty}")
        (staging / "playlist.ini").write_text("\n".join(lines) + "\n", encoding="utf-8",
                                              newline="\n")
        count = f"{len(entries)} track" + ("" if len(entries) == 1 else "s")
        try:
            card = render_card_bytes(staging / cover_name if cover_name else None,
                                     playlist.title, playlist.author or "Playlist",
                                     detail=count)
        except ValueError:
            # An undecodable image still gives a usable card.
            card = render_card_bytes(None, playlist.title, playlist.author or "Playlist",
                                     detail=count)
        (staging / CARD_FILENAME).write_bytes(card)
        files = sorted((path for path in staging.iterdir() if path.is_file()),
                       key=lambda path: path.name.casefold())
        report = {
            "bundleFormat": BUNDLE_FORMAT_VERSION,
            "kind": "playlist",
            "source": {"type": "beatsaver_playlist", "id": playlist.source_id},
            "playlist": {"title": playlist.title, "author": playlist.author,
                         "songs": len(entries), "libraryCard": CARD_FILENAME},
            "files": [{"path": path.name, "size": path.stat().st_size,
                       "sha256": sha256_file(path)} for path in files],
        }
        (staging / "conversion-report.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if destination.exists():
            shutil.rmtree(destination)
        staging.rename(destination)
        return destination
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def import_playlist(source: str, output_root: Path, *, download: bool = True,
                    force: bool = False,
                    log: Callable[[str], None] = print) -> tuple[Path, PlaylistImportReport]:
    playlist = load_playlist(source)
    log(f"Playlist: {playlist.title} ({len(playlist.entries)} songs)")
    report = resolve_entries(playlist, output_root, download=download, log=log)
    # Songs that could not be imported stay out of the bundle.
    return write_playlist_bundle(playlist, output_root, force=force), report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source", help=".bplist file, beatsaver.com/playlists/<id> URL or id")
    parser.add_argument("--output", type=Path, default=Path("out/levels"))
    parser.add_argument("--no-download", action="store_true",
                        help="only use maps already in the output library")
    parser.add_argument("--force", action="store_true",
                        help="replace an existing playlist bundle")
    args = parser.parse_args()
    try:
        bundle, report = import_playlist(args.source, args.output,
                                         download=not args.no_download, force=args.force)
    except (OSError, ImportError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"Playlist bundle: {bundle}")
    print(f"Maps: {len(report.imported)} imported, {len(report.existing)} already present, "
          f"{len(report.failed)} failed")
    for label, reason in report.failed:
        print(f"  skipped {label}: {reason}")
    print("Use downloaded content only as permitted by its rightsholders and BeatSaver's terms.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
