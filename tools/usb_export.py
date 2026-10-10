#!/usr/bin/env python3
"""Copy validated MoveBeat bundles to a USB drive the PS3 can read.

The game plays bundles found in MOVEBEAT/levels on /dev_usb000-007. The
PS3 reads FAT32 (FAT) drives only: exFAT and NTFS drives are refused with
an explanation, as are drives without room. A bundle is copied into a
hidden staging folder, every file is read back and compared by SHA-256,
and only then the folder is renamed to its level name. An existing level
on the drive is never replaced.

    python tools/usb_export.py E: out/levels/42607 out/levels/playlist-1176968
    python tools/usb_export.py E: --all out/levels
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import sys
from typing import Callable

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.validate_bundle import validate_bundle  # noqa: E402


USB_LIBRARY = ("MOVEBEAT", "levels")
SAFE_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,79}")
FAT32_MAX_FILE = 4 * 1024 * 1024 * 1024 - 1
# Filesystems the PS3 reads from USB.
PS3_FILESYSTEMS = ("FAT32", "FAT")
# Spare room kept free besides the files (directory entries, cluster slack).
SPARE_BYTES = 2 * 1024 * 1024


class UsbExportError(RuntimeError):
    pass


@dataclass
class Volume:
    root: Path
    filesystem: str
    label: str
    free_bytes: int


@dataclass
class ExportReport:
    copied: list[str] = field(default_factory=list)
    skipped: list[tuple[str, str]] = field(default_factory=list)


def list_removable_drives() -> list[Path]:
    """Removable drive roots (USB flash drives) on Windows; empty elsewhere."""
    if os.name != "nt":
        return []
    import ctypes

    kernel32 = ctypes.windll.kernel32
    mask = kernel32.GetLogicalDrives()
    drives = []
    for index in range(26):
        if not mask & (1 << index):
            continue
        root = f"{chr(ord('A') + index)}:\\"
        # DRIVE_REMOVABLE = 2
        if kernel32.GetDriveTypeW(ctypes.c_wchar_p(root)) == 2:
            drives.append(Path(root))
    return drives


def read_volume(root: Path) -> Volume:
    """Filesystem, label and free space of the volume holding `root`."""
    if os.name == "nt":
        import ctypes

        drive = str(root)[:3]
        label = ctypes.create_unicode_buffer(261)
        filesystem = ctypes.create_unicode_buffer(261)
        if not ctypes.windll.kernel32.GetVolumeInformationW(
                ctypes.c_wchar_p(drive), label, len(label), None, None, None,
                filesystem, len(filesystem)):
            raise UsbExportError(f"Не удалось прочитать носитель {drive}: вставлен ли он?")
        free = ctypes.c_ulonglong(0)
        if not ctypes.windll.kernel32.GetDiskFreeSpaceExW(
                ctypes.c_wchar_p(drive), ctypes.byref(free), None, None):
            raise UsbExportError(f"Не удалось узнать свободное место на {drive}")
        return Volume(Path(drive), filesystem.value, label.value, free.value)
    usage = shutil.disk_usage(root)
    return Volume(root, "unknown", "", usage.free)


def check_ps3_volume(volume: Volume) -> None:
    """Refuses a drive the PS3 cannot read, with what to do about it."""
    if volume.filesystem.upper() not in PS3_FILESYSTEMS:
        raise UsbExportError(
            f"Носитель {volume.root} отформатирован в {volume.filesystem or 'неизвестную ФС'}, "
            "а PS3 читает с USB только FAT32. Отформатируйте его в FAT32 "
            "(для дисков больше 32 ГБ Windows не предлагает FAT32 — подойдёт, "
            "например, Rufus или guiformat). Форматирование сотрёт данные на носителе.")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _bundle_files(directory: Path) -> list[tuple[str, int, str]]:
    errors = validate_bundle(directory)
    if errors:
        raise UsbExportError(f"Уровень {directory.name} не прошёл проверку: " + "; ".join(errors))
    report_path = directory / "conversion-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    entries = [(entry["path"], entry["size"], entry["sha256"]) for entry in report["files"]]
    report_bytes = report_path.read_bytes()
    entries.append((report_path.name, len(report_bytes),
                    hashlib.sha256(report_bytes).hexdigest()))
    for name, size, _ in entries:
        if not SAFE_NAME.fullmatch(name):
            raise UsbExportError(f"Недопустимое имя файла для FAT32: {name}")
        if size > FAT32_MAX_FILE:
            raise UsbExportError(f"Файл {name} больше 4 ГБ: на FAT32 он не поместится")
    return entries


def export_bundle(directory: Path, drive: Path, *,
                  volume_reader: Callable[[Path], Volume] = read_volume,
                  progress: Callable[[int, int], None] | None = None) -> Path:
    """Copies one bundle to DRIVE/MOVEBEAT/levels/<name>; returns that path."""
    directory = Path(directory)
    name = directory.name
    if directory.is_symlink() or not SAFE_NAME.fullmatch(name):
        raise UsbExportError(f"Недопустимое имя уровня: {name}")
    entries = _bundle_files(directory)
    volume = volume_reader(Path(drive))
    check_ps3_volume(volume)
    total = sum(size for _, size, _ in entries)
    if volume.free_bytes < total + SPARE_BYTES:
        raise UsbExportError(
            f"На {volume.root} не хватает места: нужно {total / 1048576:.1f} МиБ, "
            f"свободно {volume.free_bytes / 1048576:.1f} МиБ")
    library = Path(drive).joinpath(*USB_LIBRARY)
    library.mkdir(parents=True, exist_ok=True)
    if any(item.name.casefold() == name.casefold() for item in library.iterdir()):
        raise UsbExportError(f"Уровень {name} уже есть на флешке; он не заменён")
    stage = library / f".movebeat-copy-{name}-{secrets.token_hex(4)}"
    stage.mkdir()
    done = 0
    try:
        for filename, size, digest in entries:
            target = stage / filename
            with (directory / filename).open("rb") as source, target.open("wb") as output:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    output.write(chunk)
                    done += len(chunk)
                    if progress is not None:
                        progress(done, total * 2)
                output.flush()
                os.fsync(output.fileno())
        for filename, size, digest in entries:
            target = stage / filename
            if target.stat().st_size != size or _sha256(target) != digest:
                raise UsbExportError(f"Проверка скопированного файла не прошла: {filename}")
            done += size
            if progress is not None:
                progress(done, total * 2)
        if any(item.name.casefold() == name.casefold() for item in library.iterdir()):
            raise UsbExportError(f"Уровень {name} появился на флешке во время копирования")
        destination = library / name
        stage.rename(destination)
        return destination
    except BaseException:
        shutil.rmtree(stage, ignore_errors=True)
        raise


def export_bundles(directories: list[Path], drive: Path, *,
                   volume_reader: Callable[[Path], Volume] = read_volume,
                   progress: Callable[[int, int], None] | None = None) -> ExportReport:
    """Copies several bundles; ones already on the drive are skipped."""
    report = ExportReport()
    volume = volume_reader(Path(drive))
    check_ps3_volume(volume)
    library = Path(drive).joinpath(*USB_LIBRARY)
    present = ({item.name.casefold() for item in library.iterdir()}
               if library.is_dir() else set())
    for index, directory in enumerate(directories):
        if directory.name.casefold() in present:
            report.skipped.append((directory.name, "уже есть на флешке"))
            continue
        try:
            export_bundle(directory, drive, volume_reader=volume_reader)
            report.copied.append(directory.name)
        except UsbExportError as exc:
            report.skipped.append((directory.name, str(exc)))
            # No room or a broken drive stops the rest as well.
            if "не хватает места" in str(exc):
                break
        if progress is not None:
            progress(index + 1, len(directories))
    return report


def library_bundles(root: Path) -> list[Path]:
    """Bundle folders (levels and playlists) of a local library."""
    if not root.is_dir():
        return []
    return sorted((path for path in root.iterdir()
                   if path.is_dir() and not path.name.startswith(".") and
                   ((path / "level.ini").is_file() or (path / "playlist.ini").is_file())),
                  key=lambda path: path.name.casefold())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("drive", type=Path, help="USB drive root, e.g. E:")
    parser.add_argument("bundles", type=Path, nargs="*")
    parser.add_argument("--all", type=Path, metavar="LIBRARY",
                        help="copy every bundle of this library folder")
    args = parser.parse_args()
    drive = Path(str(args.drive).rstrip("\\/") + os.sep)
    bundles = list(args.bundles) + (library_bundles(args.all) if args.all else [])
    if not bundles:
        parser.error("name bundle folders or --all LIBRARY")
    try:
        report = export_bundles(bundles, drive)
    except (OSError, UsbExportError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    print(f"Скопировано: {len(report.copied)}; пропущено: {len(report.skipped)}")
    for name, reason in report.skipped:
        print(f"  {name}: {reason}")
    return 0 if report.copied or not report.skipped else 1


if __name__ == "__main__":
    raise SystemExit(main())
