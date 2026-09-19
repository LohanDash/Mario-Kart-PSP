#!/usr/bin/env python3
"""Convert an MKDS KCL prism file to explicit PSP collision triangles."""
import argparse
import math
import struct
from collections import Counter
from pathlib import Path


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def add(a, b, scale=1.0):
    return tuple(x + y*scale for x, y in zip(a, b))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--scale", type=float, default=1.0/64.0)
    parser.add_argument("--offset", type=float, nargs=3, default=(-0.77734375, -8.55078125, -8.474609375))
    args = parser.parse_args()
    data = args.source.read_bytes()
    vertex_off, normal_off, prism_base, octree_off = struct.unpack_from("<4I", data)
    prism_off = prism_base + 0x10
    positions = [struct.unpack_from("<iii", data, p) for p in range(vertex_off, normal_off, 12)]
    positions = [tuple(v/4096.0 for v in p) for p in positions]
    normal_end = prism_off
    normals = [struct.unpack_from("<hhh", data, p) for p in range(normal_off, normal_end-5, 6)]
    normals = [tuple(v/4096.0 for v in n) for n in normals]
    output = bytearray()
    types = Counter()
    skipped = 0
    for offset in range(prism_off, octree_off, 16):
        length_raw, vi, ni, ai, bi, ci, attributes = struct.unpack_from("<i6H", data, offset)
        if vi >= len(positions) or max(ni, ai, bi, ci) >= len(normals):
            skipped += 1
            continue
        origin, normal = positions[vi], normals[ni]
        edge_a, edge_b, edge_c = normals[ai], normals[bi], normals[ci]
        length = length_raw / 4096.0
        cross_a, cross_b = cross(edge_a, normal), cross(edge_b, normal)
        denom_a, denom_b = dot(cross_a, edge_c), dot(cross_b, edge_c)
        if abs(denom_a) < 1e-7 or abs(denom_b) < 1e-7:
            skipped += 1
            continue
        b = add(origin, cross_b, length / denom_b)
        c = add(origin, cross_a, length / denom_a)
        converted = []
        for point in (origin, b, c):
            converted.extend((point[0]*args.scale+args.offset[0],
                              point[1]*args.scale+args.offset[1],
                              point[2]*args.scale+args.offset[2]))
        collision_type = (attributes >> 8) & 0x1f
        variant = (attributes >> 5) & 7
        is_wall = 1 if attributes & 0x4000 else 0
        is_floor = 1 if attributes & 0x8000 else 0
        if collision_type == 15: category = 5       # cannon
        elif collision_type in (10, 11, 17): category = 6  # out/fall
        elif collision_type in (7, 12, 18): category = 4   # boost/jump
        elif is_wall or collision_type in (8, 9, 14, 16, 21): category = 2
        elif is_floor: category = 1
        else: category = 7                           # special/non-solid
        face = normal
        output += struct.pack("<12fH4Bxx", *(converted + list(face)), attributes,
                              collision_type, variant, category, 0)
        types[(collision_type, variant, category)] += 1
    args.destination.parent.mkdir(parents=True, exist_ok=True)
    args.destination.write_bytes(struct.pack("<4sI", b"MKCL", len(output)//56) + output)
    print("triangles", len(output)//56, "skipped", skipped)
    print("types", sorted(types.items()))


if __name__ == "__main__":
    main()
