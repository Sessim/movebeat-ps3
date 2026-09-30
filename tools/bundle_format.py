"""Dependency-free constants and hashing for prepared MoveBeat bundles."""

from __future__ import annotations

import hashlib
from pathlib import Path


BUNDLE_FORMAT_VERSION = 1
CARD_FILENAME = "library.mblc"
CARD_WIDTH = 512
CARD_HEIGHT = 96
CARD_MAGIC = b"MBLC"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()
