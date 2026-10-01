"""Render a compact, UTF-8-capable track card for the PS3 software renderer.

MBLC v1 is 512x96 RGB565, big-endian: four magic bytes, big-endian u16 width
and height, then exactly width*height big-endian u16 pixels. The original
cover remains in the bundle; this file is a derived display asset.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import struct

from PIL import Image, ImageDraw, ImageFont, ImageOps, UnidentifiedImageError
from tools.bundle_format import CARD_FILENAME, CARD_HEIGHT, CARD_MAGIC, CARD_WIDTH


COVER_SIZE = 96
MAX_COVER_PIXELS = 16_000_000
Image.MAX_IMAGE_PIXELS = MAX_COVER_PIXELS


def _font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = (
        Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts" / "segoeui.ttf",
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"),
        Path("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"),
    )
    for path in candidates:
        if path.is_file():
            try:
                return ImageFont.truetype(str(path), size)
            except OSError:
                continue
    return ImageFont.load_default(size=size)


def _clean_label(value: str) -> str:
    return re.sub(r"[\x00-\x1f\x7f]+", " ", value).strip() or "Unknown"


def _fit_text(draw: ImageDraw.ImageDraw, value: str,
              font: ImageFont.FreeTypeFont | ImageFont.ImageFont,
              maximum_width: int) -> str:
    value = _clean_label(value)
    if draw.textlength(value, font=font) <= maximum_width:
        return value
    low, high = 0, len(value)
    while low < high:
        middle = (low + high + 1) // 2
        if draw.textlength(value[:middle] + "…", font=font) <= maximum_width:
            low = middle
        else:
            high = middle - 1
    return value[:low].rstrip() + "…"


def render_card_bytes(cover: Path | None, title: str, author: str,
                      mapper: str = "", detail: str = "") -> bytes:
    """`mapper` prints as "Mapper: ..."; `detail` replaces that line as is."""
    card = Image.new("RGB", (CARD_WIDTH, CARD_HEIGHT), (14, 16, 29))
    draw = ImageDraw.Draw(card)
    if cover is not None:
        try:
            with Image.open(cover) as source:
                if source.width * source.height > MAX_COVER_PIXELS:
                    raise ValueError("cover image is too large")
                thumbnail = ImageOps.fit(source.convert("RGB"),
                                         (COVER_SIZE, COVER_SIZE),
                                         Image.Resampling.LANCZOS)
        except (OSError, ValueError, Image.DecompressionBombError,
                UnidentifiedImageError) as exc:
            raise ValueError(f"cannot decode cover image: {exc}") from exc
        card.paste(thumbnail, (0, 0))
    else:
        draw.rectangle((0, 0, COVER_SIZE - 1, COVER_SIZE - 1),
                       fill=(28, 47, 78))
        draw.line((18, 68, 47, 27, 75, 68), fill=(78, 198, 248), width=8)
    draw.rectangle((COVER_SIZE, 0, COVER_SIZE + 4, CARD_HEIGHT - 1),
                   fill=(39, 51, 81))
    title_font = _font(21)
    author_font = _font(15)
    mapper_font = _font(12)
    label_width = CARD_WIDTH - COVER_SIZE - 28
    draw.text((COVER_SIZE + 18, 9),
              _fit_text(draw, title, title_font, label_width),
              font=title_font, fill=(242, 246, 255))
    draw.text((COVER_SIZE + 18, 43),
              _fit_text(draw, author, author_font, label_width),
              font=author_font, fill=(150, 179, 211))
    third_line = detail.strip() or (("Mapper: " + mapper) if mapper.strip() else "")
    if third_line:
        draw.text((COVER_SIZE + 18, 71),
                  _fit_text(draw, third_line, mapper_font, label_width),
                  font=mapper_font, fill=(126, 145, 180))

    rgb = card.tobytes()
    payload = bytearray(CARD_WIDTH * CARD_HEIGHT * 2)
    for index in range(CARD_WIDTH * CARD_HEIGHT):
        offset = index * 3
        red, green, blue = rgb[offset], rgb[offset + 1], rgb[offset + 2]
        pixel = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
        struct.pack_into(">H", payload, index * 2, pixel)
    return CARD_MAGIC + struct.pack(">HH", CARD_WIDTH, CARD_HEIGHT) + payload


def write_card(directory: Path, cover_name: str, title: str,
               author: str, mapper: str = "") -> Path:
    cover = directory / cover_name if cover_name else None
    if cover is not None and not cover.is_file():
        raise ValueError(f"cover image is missing: {cover_name}")
    output = directory / CARD_FILENAME
    output.write_bytes(render_card_bytes(cover, title, author, mapper))
    return output


def upgrade_bundle_card(directory: Path) -> Path:
    """Add or regenerate a card in an existing validated bundle."""
    from tools.validate_bundle import validate_bundle

    errors = validate_bundle(directory)
    if errors:
        raise ValueError("bundle must validate before upgrade: " + "; ".join(errors))
    manifest = dict(line.split("=", 1) for line in
                    (directory / "level.ini").read_text(encoding="utf-8").splitlines()
                    if "=" in line)
    output = write_card(directory, manifest.get("cover", ""),
                        manifest.get("title", ""),
                        manifest.get("song_author", ""),
                        manifest.get("mapper", ""))
    report_path = directory / "conversion-report.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    report["level"]["libraryCard"] = CARD_FILENAME
    files = []
    for path in sorted(directory.iterdir(), key=lambda item: item.name.casefold()):
        if not path.is_file() or path.name == report_path.name:
            continue
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        files.append({"path": path.name, "size": path.stat().st_size,
                      "sha256": digest.hexdigest()})
    report["files"] = files
    temporary = report_path.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                         encoding="utf-8")
    temporary.replace(report_path)
    return output


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundles", type=Path, nargs="+")
    arguments = parser.parse_args()
    for bundle in arguments.bundles:
        result = upgrade_bundle_card(bundle)
        print(result)
