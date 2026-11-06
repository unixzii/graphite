"""Regression tests for exact capture comparison without image dependencies."""
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock
import zlib

import compare


def png(width, height, pixels, filter_kind=0):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    stride = width * 4
    encoded = bytearray()
    previous = bytes(stride)
    for y in range(height):
        row = pixels[y * stride:(y + 1) * stride]
        encoded.append(filter_kind)
        for i, value in enumerate(row):
            a = row[i - 4] if i >= 4 else 0
            b = previous[i]
            c = previous[i - 4] if i >= 4 else 0
            p = a + b - c
            pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
            predictor = (0, a, b, (a + b) // 2, a if pa <= pb and pa <= pc else b if pb <= pc else c)[filter_kind]
            encoded.append((value - predictor) & 255)
        previous = row
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(encoded)) + chunk(b"IEND", b""))


class CaptureComparison(unittest.TestCase):
    def test_all_png_filters(self):
        pixels = bytes((i * 71) & 255 for i in range(24))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "capture.png"
            for kind in range(5):
                path.write_bytes(png(3, 2, pixels, kind))
                width, height, result = compare.read_png(path)
                self.assertEqual((width, height, result), (3, 2, pixels))

    def test_exact_comparison_and_missing_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / "a", Path(tmp) / "b"
            a.mkdir()
            b.mkdir()
            (a / "one.png").write_bytes(png(1, 1, bytes([0, 2, 3, 255])))
            (b / "one.png").write_bytes(png(1, 1, bytes([0, 2, 3, 255]), 4))
            self.assertEqual(compare.compare(a, b)["identical"], 1)
            (b / "one.png").write_bytes(png(1, 1, bytes([0, 2, 4, 255])))
            report = compare.compare(a, b)
            self.assertEqual(report["different"][0]["max_channel_error"], 1)
            self.assertEqual(report["different"][0]["changed_pixels"], 1)
            (a / "extra.png").write_bytes(png(1, 1, bytes(4)))
            self.assertEqual(compare.compare(a, b)["missing"], ["extra.png"])
            with mock.patch("sys.argv", ["compare.py", str(a), str(b)]), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(compare.main(), 1)

    def test_metrics(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "run.log"
            path.write_text("test example ... [macos] blur@1.5x sigma: 4.001 (expected 4.000 ± 0.500)\n")
            self.assertEqual(compare.measurements(path), {"macos::blur@1.5x sigma": [(4.001, 4.0, 0.5)]})
            path.write_text(
                "[gtk-gl] blur@1x sigma: 4.000 (expected 4.000 ± 0.500)\n"
                "[gtk-cairo] blur@1x sigma: 3.900 (expected 4.000 ± 0.500)\n"
            )
            self.assertEqual(len(compare.measurements(path)), 2)
            path.write_text("no measurements\n")
            with self.assertRaises(ValueError):
                compare.measurements(path)


if __name__ == "__main__":
    unittest.main(verbosity=2)
