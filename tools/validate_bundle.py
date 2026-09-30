#!/usr/bin/env python3
"""Validate a MoveBeat Level Bundle and all hashes in its conversion report."""

from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePath
import re
import sys
from typing import Any

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tools.bundle_format import (
    BUNDLE_FORMAT_VERSION, CARD_FILENAME, CARD_HEIGHT, CARD_MAGIC, CARD_WIDTH,
    sha256_file,
)


def _safe_leaf(value: Any) -> bool:
    if not isinstance(value, str) or not value or value in (".", ".."):
        return False
    path = PurePath(value)
    return not path.is_absolute() and len(path.parts) == 1 and "/" not in value and "\\" not in value


def validate_bundle(directory: Path) -> list[str]:
    errors: list[str] = []
    if not directory.is_dir():
        return [f"bundle directory does not exist: {directory}"]
    report_path = directory / "conversion-report.json"
    try:
        report = json.loads(report_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        return [f"cannot read conversion-report.json: {exc}"]
    if not isinstance(report, dict):
        return ["conversion-report.json root must be an object"]
    if report.get("bundleFormat") != BUNDLE_FORMAT_VERSION:
        errors.append(f"unsupported bundleFormat: {report.get('bundleFormat')!r}")
    source = report.get("source")
    if not isinstance(source, dict):
        errors.append("source must be an object")
    else:
        if source.get("type") not in ("beatsaver", "beat_saber_zip"):
            errors.append(f"unsupported source.type: {source.get('type')!r}")
        if not isinstance(source.get("key"), str):
            errors.append("source.key must be a string")
        for field in ("contentSha1", "archiveSha1"):
            digest = source.get(field)
            if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{40}", digest):
                errors.append(f"source.{field} must be a lowercase SHA-1")
    entries = report.get("files")
    if not isinstance(entries, list):
        return errors + ["files must be an array"]

    expected: dict[str, str] = {}
    for index, entry in enumerate(entries):
        label = f"files[{index}]"
        if not isinstance(entry, dict):
            errors.append(f"{label} must be an object")
            continue
        name = entry.get("path")
        if not _safe_leaf(name):
            errors.append(f"{label}.path is not a safe leaf")
            continue
        folded = name.casefold()
        if folded in expected:
            errors.append(f"duplicate file path: {name}")
            continue
        expected[folded] = name
        path = directory / name
        if not path.is_file() or path.is_symlink():
            errors.append(f"missing regular file: {name}")
            continue
        size = entry.get("size")
        if not isinstance(size, int) or size < 0 or path.stat().st_size != size:
            errors.append(f"size mismatch: {name}")
        digest = entry.get("sha256")
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            errors.append(f"invalid SHA-256 field: {name}")
        elif sha256_file(path) != digest:
            errors.append(f"SHA-256 mismatch: {name}")

    actual: dict[str, str] = {}
    for path in directory.iterdir():
        if path.name == report_path.name:
            continue
        if not path.is_file() or path.is_symlink():
            errors.append(f"bundle contains a non-regular entry: {path.name}")
            continue
        folded = path.name.casefold()
        if folded in actual:
            errors.append(f"case-insensitive duplicate on disk: {path.name}")
        actual[folded] = path.name
    for folded, name in actual.items():
        if folded not in expected:
            errors.append(f"unlisted file: {name}")
    if "level.ini" not in expected:
        errors.append("level.ini is not listed")
    card_name = expected.get(CARD_FILENAME)
    if card_name is not None:
        card = directory / card_name
        if card.is_file() and not card.is_symlink():
            header = CARD_MAGIC + CARD_WIDTH.to_bytes(2, "big") + CARD_HEIGHT.to_bytes(2, "big")
            with card.open("rb") as stream:
                actual_header = stream.read(8)
            if actual_header != header or card.stat().st_size != 8 + CARD_WIDTH * CARD_HEIGHT * 2:
                errors.append("library.mblc is not a 512x96 RGB565 card")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    args = parser.parse_args()
    errors = validate_bundle(args.bundle)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print(f"Valid MoveBeat bundle: {args.bundle}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
