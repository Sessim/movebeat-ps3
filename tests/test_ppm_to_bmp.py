from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from tools.ppm_to_bmp import convert_ppm_to_bmp


class PpmToBmpTests(unittest.TestCase):
    def test_converts_padded_rows_to_bottom_up_bgr(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "frame.ppm"
            destination = directory / "frame.bmp"
            source.write_bytes(
                b"P6\n# RPCS3 test frame\n2 2\n255\n"
                + bytes((255, 0, 0, 0, 255, 0,
                         0, 0, 255, 255, 255, 255))
            )

            convert_ppm_to_bmp(source, destination)

            bmp = destination.read_bytes()
            self.assertEqual(bmp[:2], b"BM")
            self.assertEqual(struct.unpack_from("<ii", bmp, 18), (2, 2))
            self.assertEqual(struct.unpack_from("<H", bmp, 28)[0], 24)
            self.assertEqual(len(bmp), 54 + 8 * 2)
            self.assertEqual(
                bmp[54:],
                bytes((255, 0, 0, 255, 255, 255, 0, 0,
                       0, 0, 255, 0, 255, 0, 0, 0)),
            )

    def test_rejects_incomplete_pixel_data(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / "short.ppm"
            source.write_bytes(b"P6\n1 1\n255\n\x00\x00")
            with self.assertRaisesRegex(ValueError, "pixel data length"):
                convert_ppm_to_bmp(source, directory / "short.bmp")


if __name__ == "__main__":
    unittest.main()
