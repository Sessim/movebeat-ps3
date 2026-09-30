#!/usr/bin/env python3
"""Convert an 8-bit binary PPM (P6) image to an uncompressed BMP."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def _token(data: bytes, index: int) -> tuple[bytes, int]:
    while index < len(data):
        value = data[index]
        if value in b" \t\r\n\v\f":
            index += 1
            continue
        if value == ord("#"):
            newline = data.find(b"\n", index)
            if newline < 0:
                raise ValueError("unterminated PPM header comment")
            index = newline + 1
            continue
        break
    start = index
    while index < len(data) and data[index] not in b" \t\r\n\v\f#":
        index += 1
    if start == index:
        raise ValueError("incomplete PPM header")
    return data[start:index], index


def convert_ppm_to_bmp(source: Path, destination: Path) -> None:
    data = source.read_bytes()
    magic, index = _token(data, 0)
    if magic != b"P6":
        raise ValueError("expected binary PPM magic P6")
    width_token, index = _token(data, index)
    height_token, index = _token(data, index)
    maximum_token, index = _token(data, index)
    try:
        width, height, maximum = (int(width_token), int(height_token),
                                  int(maximum_token))
    except ValueError as error:
        raise ValueError("invalid PPM dimensions or maximum value") from error
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError("only positive dimensions and 8-bit PPM are supported")

    if index >= len(data) or data[index] not in b" \t\r\n\v\f":
        raise ValueError("PPM header is not separated from image data")
    if data[index:index + 2] == b"\r\n":
        index += 2
    else:
        index += 1

    rgb = data[index:]
    row_bytes = width * 3
    if len(rgb) != row_bytes * height:
        raise ValueError("PPM pixel data length does not match its dimensions")

    row_stride = (row_bytes + 3) & ~3
    image_size = row_stride * height
    bmp = bytearray(54 + image_size)
    struct.pack_into("<2sIHHI", bmp, 0, b"BM", len(bmp), 0, 0, 54)
    struct.pack_into("<IiiHHIIiiII", bmp, 14, 40, width, height,
                     1, 24, 0, image_size, 2835, 2835, 0, 0)

    for y in range(height):
        source_row = y * row_bytes
        target_row = 54 + (height - 1 - y) * row_stride
        for x in range(width):
            source_pixel = source_row + x * 3
            target_pixel = target_row + x * 3
            bmp[target_pixel] = rgb[source_pixel + 2]
            bmp[target_pixel + 1] = rgb[source_pixel + 1]
            bmp[target_pixel + 2] = rgb[source_pixel]

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(bmp)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="input P6 PPM image")
    parser.add_argument("destination", type=Path, help="output BMP image")
    arguments = parser.parse_args()
    try:
        convert_ppm_to_bmp(arguments.source, arguments.destination)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
