#!/usr/bin/env python3
"""Compare decoded RGBA8 conformance captures and optional measurement logs.

Uses only the Python standard library. PNG metadata/compression differences do
not affect comparison. Both capture writers must emit non-interlaced RGBA8 PNGs.
"""

import argparse
import collections
import json
import math
from pathlib import Path
import re
import struct
import sys
import zlib


def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    offset = 8
    compressed = bytearray()
    header = None
    ended = False
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + length]
        crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
        if zlib.crc32(kind + payload) != crc:
            raise ValueError(f"{path}: invalid chunk checksum")
        offset += length + 12
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            ended = True
            break
    if not ended or header is None:
        raise ValueError(f"{path}: incomplete PNG")
    width, height, depth, color, compression, filtering, interlace = header
    if (depth, color, compression, filtering, interlace) != (8, 6, 0, 0, 0):
        raise ValueError(f"{path}: expected non-interlaced RGBA8, got {header}")
    stride = width * 4
    raw = zlib.decompress(compressed)
    if len(raw) != (stride + 1) * height:
        raise ValueError(f"{path}: incorrect decoded size")
    pixels = bytearray()
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        row = bytearray(raw[start + 1:start + 1 + stride])
        if kind > 4:
            raise ValueError(f"{path}: invalid scanline filter {kind}")
        for x in range(stride):
            left = row[x - 4] if x >= 4 else 0
            above = previous[x]
            upper_left = previous[x - 4] if x >= 4 else 0
            if kind == 1:
                predictor = left
            elif kind == 2:
                predictor = above
            elif kind == 3:
                predictor = (left + above) // 2
            elif kind == 4:
                p = left + above - upper_left
                distances = (abs(p - left), abs(p - above), abs(p - upper_left))
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            else:
                predictor = 0
            row[x] = (row[x] + predictor) & 255
        pixels.extend(row)
        previous = row
    return width, height, pixels


def measurements(path):
    pattern = re.compile(
        r"\[[^\]]+\] (\S+@\S+x) (.+): "
        r"([-+\d.eE]+) \(expected ([-+\d.eE]+) ± ([-+\d.eE]+)\)"
    )
    found = collections.defaultdict(list)
    for line in path.read_text().splitlines():
        match = pattern.search(line)
        if match:
            key = f"{match[1]} {match[2]}"
            found[key].append(tuple(float(match[i]) for i in (3, 4, 5)))
    if not found:
        raise ValueError(f"{path}: no measurements found")
    return dict(found)


def compare(left, right):
    left_files = {p.relative_to(left) for p in left.rglob("*.png")}
    right_files = {p.relative_to(right) for p in right.rglob("*.png")}
    if not left_files or not right_files:
        raise ValueError("Both directories must contain PNG captures")
    report = {
        "reference_count": len(left_files),
        "candidate_count": len(right_files),
        "missing": sorted(map(str, left_files - right_files)),
        "extra": sorted(map(str, right_files - left_files)),
        "identical": 0,
        "different": [],
    }
    for name in sorted(left_files & right_files):
        aw, ah, a = read_png(left / name)
        bw, bh, b = read_png(right / name)
        if (aw, ah) != (bw, bh):
            report["different"].append({"file": str(name), "reference_size": [aw, ah], "candidate_size": [bw, bh]})
            continue
        if a == b:
            report["identical"] += 1
            continue
        mismatches = [i for i in range(0, len(a), 4) if a[i:i + 4] != b[i:i + 4]]
        differences = [abs(x - y) for x, y in zip(a, b)]
        report["different"].append({
            "file": str(name),
            "changed_pixels": len(mismatches),
            "total_pixels": aw * ah,
            "max_channel_error": max(differences),
            "rmse": math.sqrt(sum(d * d for d in differences) / len(differences)),
            "first_difference": {
                "x": mismatches[0] // 4 % aw,
                "y": mismatches[0] // 4 // aw,
                "reference": list(a[mismatches[0]:mismatches[0] + 4]),
                "candidate": list(b[mismatches[0]:mismatches[0] + 4]),
            },
        })
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--reference-log", type=Path)
    parser.add_argument("--candidate-log", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if bool(args.reference_log) != bool(args.candidate_log):
        parser.error("Supply both measurement logs or neither")
    try:
        report = compare(args.reference, args.candidate)
        if args.reference_log:
            a = measurements(args.reference_log)
            b = measurements(args.candidate_log)
            report["measurement_groups"] = len(a)
            report["measurement_differences"] = [
                {"key": key, "reference": a.get(key), "candidate": b.get(key)}
                for key in sorted(a.keys() | b.keys()) if a.get(key) != b.get(key)
            ]
        output = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(output)
        print(output, end="")
        return int(bool(report["missing"] or report["extra"] or report["different"] or report.get("measurement_differences")))
    except (ValueError, OSError, struct.error, zlib.error) as error:
        print(f"conformance comparison: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
