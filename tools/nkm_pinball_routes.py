#!/usr/bin/env python3
"""Generate compact C route data for Waluigi Pinball's three iron balls."""
import struct
from pathlib import Path

root = Path(__file__).resolve().parents[1]
data = (root / "assets/courses/pinball/course_map.nkm").read_bytes()
path_offset = data.find(b"PATH")
point_offset = data.find(b"POIT") + 8
path_count = struct.unpack_from("<I", data, path_offset + 4)[0]
counts = [struct.unpack_from("<H", data, path_offset + 8 + i * 4 + 2)[0]
          for i in range(path_count)]

lines = ["/* Generated from course_map.nkm; do not edit manually. */"]
for route in (4, 5, 6):
    start = sum(counts[:route])
    points = []
    for index in range(counts[route]):
        x, y, z = struct.unpack_from("<iii", data, point_offset + (start + index) * 20)
        points.append((x / 262144.0 - 0.77734375,
                       y / 262144.0 - 8.55078125,
                       z / 262144.0 - 8.474609375))
    lines.append(f"static const Vec3 pinball_ball_route_{route}[] = {{")
    lines.extend(f"    {{{x:.5f}f,{y:.5f}f,{z:.5f}f}}," for x, y, z in points)
    lines.append("};")
(root / "src/pinball_routes.h").write_text("\n".join(lines) + "\n", encoding="ascii")
