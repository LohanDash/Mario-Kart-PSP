#!/usr/bin/env python3
"""Subdivide an existing PSP MKT triangle stream without changing its texture."""

import argparse
import math
import struct
from pathlib import Path


def midpoint(a, b):
    # u, v, packed colour, x, y, z. Colours are identical in our course data.
    return ((a[0] + b[0]) * .5, (a[1] + b[1]) * .5, a[2],
            (a[3] + b[3]) * .5, (a[4] + b[4]) * .5, (a[5] + b[5]) * .5)


def split(triangle, maximum, depth=0):
    edges = ((0, 1), (1, 2), (2, 0))
    lengths = [math.dist(triangle[a][3:6], triangle[b][3:6]) for a, b in edges]
    longest = max(range(3), key=lengths.__getitem__)
    if lengths[longest] <= maximum or depth >= 10:
        yield triangle
        return
    a, b = edges[longest]
    c = 3 - a - b
    middle = midpoint(triangle[a], triangle[b])
    yield from split((triangle[a], middle, triangle[c]), maximum, depth + 1)
    yield from split((middle, triangle[b], triangle[c]), maximum, depth + 1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--max-edge", type=float, required=True)
    args = parser.parse_args()
    data = args.path.read_bytes()
    magic, count, stride, dimensions = struct.unpack_from("<4sIII", data)
    if stride != 24 or count % 3:
        raise ValueError("unsupported MKT stream")
    vertices = [struct.unpack_from("<ffIfff", data, 16 + index * stride)
                for index in range(count)]
    output = []
    for index in range(0, count, 3):
        for triangle in split(tuple(vertices[index:index + 3]), args.max_edge):
            output.extend(triangle)
    payload = b"".join(struct.pack("<ffIfff", *vertex) for vertex in output)
    args.path.write_bytes(struct.pack("<4sIII", magic, len(output), stride, dimensions) + payload)
    print(args.path, count, "->", len(output), "vertices")


if __name__ == "__main__":
    main()
