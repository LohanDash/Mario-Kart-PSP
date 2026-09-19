#!/usr/bin/env python3
"""Convert a Wavefront OBJ into a compact, untextured PSP triangle stream."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path

from PIL import Image


PALETTE = [
    0xFF505050, 0xFFC08040, 0xFFB0B0B0, 0xFF3030D0, 0xFF406060, 0xFF30A030,
    0xFF404040, 0xFF50B050, 0xFF909090, 0xFF30A0D0, 0xFF80A0D0, 0xFF707070,
    0xFF305090, 0xFF4060A0, 0xFF707040, 0xFF609060, 0xFF506050, 0xFF4080B0,
    0xFF606090, 0xFF606060, 0xFF707070, 0xFF30A040,
]


def swizzle_rgba(data: bytes, width: int, height: int) -> bytes:
    """Arrange 32-bit pixels in the PSP GE's 16-byte by 8-row tiles."""
    row_bytes = width * 4
    if row_bytes % 16 or height % 8:
        raise ValueError("swizzled PSP textures require 16-byte rows and 8-row height")
    output = bytearray()
    for y in range(0, height, 8):
        for x in range(0, row_bytes, 16):
            for row in range(8):
                start = (y + row) * row_bytes + x
                output += data[start:start + 16]
    return bytes(output)


def read_materials(path: Path) -> dict[str, Path]:
    materials: dict[str, Path] = {}
    current = ""
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = raw.split(maxsplit=1)
        if len(fields) != 2:
            continue
        if fields[0] == "newmtl":
            current = fields[1]
        elif fields[0] == "map_Kd" and current:
            materials[current] = path.parent / fields[1]
    return materials


def build_atlas(materials: dict[str, Path], output: Path) -> tuple[dict[str, tuple[int, int, int, int]], int, int]:
    images = {name: Image.open(path).convert("RGBA") for name, path in materials.items()}
    atlas_width = atlas_height = 512
    placements: dict[str, tuple[int, int, int, int]] = {}
    occupied = [[False] * (atlas_width // 8) for _ in range(atlas_height // 8)]
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (255, 255, 255, 255))
    for name, image in sorted(images.items(), key=lambda item: item[1].height, reverse=True):
        width, height = image.size
        padding = 0 if width >= 512 or height >= 512 else 2
        packed_width = width + padding * 2
        packed_height = height + padding * 2
        cell_width = (packed_width + 7) // 8
        cell_height = (packed_height + 7) // 8
        chosen = None
        for cell_y in range(0, len(occupied) - cell_height + 1):
            for cell_x in range(0, len(occupied[0]) - cell_width + 1):
                if all(not occupied[y][x]
                       for y in range(cell_y, cell_y + cell_height)
                       for x in range(cell_x, cell_x + cell_width)):
                    chosen = (cell_x, cell_y)
                    break
            if chosen:
                break
        if not chosen:
            raise ValueError("textures do not fit in the 512x512 PSP atlas")
        cell_x, cell_y = chosen
        for y in range(cell_y, cell_y + cell_height):
            for x in range(cell_x, cell_x + cell_width):
                occupied[y][x] = True
        outer_x, outer_y = cell_x * 8, cell_y * 8
        x, y = outer_x + padding, outer_y + padding
        placements[name] = (x, y, width, height)
        atlas.paste(image, (x, y))
        if padding:
            atlas.paste(image.crop((0, 0, width, 1)).resize((width, padding)), (x, outer_y))
            atlas.paste(image.crop((0, height - 1, width, height)).resize((width, padding)), (x, y + height))
            atlas.paste(image.crop((0, 0, 1, height)).resize((padding, height)), (outer_x, y))
            atlas.paste(image.crop((width - 1, 0, width, height)).resize((padding, height)), (x + width, y))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(atlas.tobytes())
    return placements, atlas_width, atlas_height


def clip_polygon(polygon, axis: int, boundary: float, keep_greater: bool):
    """Clip [(u,v,x,y,z), ...] against one UV half-plane."""
    result = []
    for current, following in zip(polygon, polygon[1:] + polygon[:1]):
        current_inside = current[axis] >= boundary if keep_greater else current[axis] <= boundary
        following_inside = following[axis] >= boundary if keep_greater else following[axis] <= boundary
        if current_inside:
            result.append(current)
        if current_inside != following_inside:
            denominator = following[axis] - current[axis]
            amount = 0.0 if abs(denominator) < 1e-12 else (boundary - current[axis]) / denominator
            result.append(tuple(a + (b - a) * amount for a, b in zip(current, following)))
    return result


def tiled_triangles(triangle):
    """Split a textured triangle at integer UV borders for an atlas with clamp."""
    min_u, max_u = min(v[0] for v in triangle), max(v[0] for v in triangle)
    min_v, max_v = min(v[1] for v in triangle), max(v[1] for v in triangle)
    first_u, last_u = math.floor(min_u), math.ceil(max_u) - 1
    first_v, last_v = math.floor(min_v), math.ceil(max_v) - 1
    if last_u < first_u: last_u = first_u
    if last_v < first_v: last_v = first_v
    for tile_u in range(first_u, last_u + 1):
        for tile_v in range(first_v, last_v + 1):
            polygon = list(triangle)
            polygon = clip_polygon(polygon, 0, tile_u, True)
            if polygon: polygon = clip_polygon(polygon, 0, tile_u + 1, False)
            if polygon: polygon = clip_polygon(polygon, 1, tile_v, True)
            if polygon: polygon = clip_polygon(polygon, 1, tile_v + 1, False)
            for index in range(1, len(polygon) - 1):
                yield tile_u, tile_v, (polygon[0], polygon[index], polygon[index + 1])


def spatial_triangles(triangle, max_edge: float, depth: int = 0):
    """Split only oversized world-space edges while interpolating UVs."""
    edges = ((0, 1), (1, 2), (2, 0))
    lengths = []
    for first, second in edges:
        a, b = triangle[first], triangle[second]
        lengths.append((a[2] - b[2]) ** 2 + (a[3] - b[3]) ** 2 + (a[4] - b[4]) ** 2)
    longest = max(range(3), key=lengths.__getitem__)
    if lengths[longest] <= max_edge * max_edge or depth >= 10:
        yield triangle
        return
    first, second = edges[longest]
    third = 3 - first - second
    midpoint = tuple((a + b) * 0.5 for a, b in zip(triangle[first], triangle[second]))
    yield from spatial_triangles((triangle[first], midpoint, triangle[third]), max_edge, depth + 1)
    yield from spatial_triangles((midpoint, triangle[second], triangle[third]), max_edge, depth + 1)


def convert(source: Path, destination: Path, scale: float, mtl: Path, texture_output: Path,
            only_materials: set[str] | None = None, no_tile: bool = False,
            direct_texture: bool = False, max_edge: float = 3.0) -> tuple[int, tuple[float, ...]]:
    positions: list[tuple[float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    faces: list[tuple[list[tuple[int, int]], str]] = []
    material = ""
    for raw in source.read_text(encoding="utf-8", errors="replace").splitlines():
        fields = raw.split()
        if not fields:
            continue
        if fields[0] == "v" and len(fields) >= 4:
            positions.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "vt" and len(fields) >= 3:
            texcoords.append(tuple(map(float, fields[1:3])))
        elif fields[0] == "usemtl" and len(fields) >= 2:
            material = fields[1]
        elif fields[0] == "f" and len(fields) >= 4:
            indices: list[tuple[int, int]] = []
            for field in fields[1:]:
                parts = field.split("/")
                vertex_index = int(parts[0])
                texture_index = int(parts[1]) if len(parts) > 1 and parts[1] else 0
                indices.append((vertex_index - 1 if vertex_index > 0 else len(positions) + vertex_index,
                                texture_index - 1 if texture_index > 0 else -1))
            faces.append((indices, material))
    if not positions or not faces:
        raise ValueError("OBJ contains no usable geometry")

    xs, ys, zs = zip(*positions)
    center_x = (min(xs) + max(xs)) * 0.5
    center_z = (min(zs) + max(zs)) * 0.5
    floor_y = min(ys)
    transformed = [
        ((x - center_x) * scale, (y - floor_y) * scale, (z - center_z) * scale)
        for x, y, z in positions
    ]
    if only_materials:
        faces = [(indices, name) for indices, name in faces if name in only_materials]
    material_images = read_materials(mtl)
    if direct_texture:
        used = sorted({name for _, name in faces})
        if len(used) != 1:
            raise ValueError("--direct-texture requires exactly one selected material")
        image = Image.open(material_images[used[0]]).convert("RGBA")
        atlas_width, atlas_height = image.size
        texture_output.parent.mkdir(parents=True, exist_ok=True)
        rgba = image.tobytes()
        texture_output.write_bytes(swizzle_rgba(rgba, atlas_width, atlas_height))
        direct_has_alpha = image.getchannel("A").getextrema()[0] < 255
        placements = {used[0]: (0, 0, atlas_width, atlas_height)}
    else:
        placements, atlas_width, atlas_height = build_atlas(material_images, texture_output)
    records = bytearray()
    vertex_count = 0
    for indices, material_name in faces:
        placement = placements.get(material_name, (0, 0, 1, 1))
        px, py, texture_width, texture_height = placement
        for i in range(1, len(indices) - 1):
            triangle = []
            for vertex_index, texture_index in (indices[0], indices[i], indices[i + 1]):
                if vertex_index < 0 or vertex_index >= len(transformed):
                    raise ValueError(f"invalid OBJ vertex index {vertex_index}")
                if 0 <= texture_index < len(texcoords):
                    u, v = texcoords[texture_index]
                else:
                    u = v = 0.0
                x, y, z = transformed[vertex_index]
                triangle.append((u, v, x, y, z))
            if direct_texture:
                chunks = ((0, 0, part) for part in spatial_triangles(tuple(triangle), max_edge))
            elif no_tile:
                chunks = [(0, 0, tuple(triangle))]
            else:
                chunks = tiled_triangles(triangle)
            for tile_u, tile_v, clipped in chunks:
                for u, v, x, y, z in clipped:
                    if direct_texture:
                        atlas_u, atlas_v = u, 1.0 - v
                    else:
                        local_u = min(1.0, max(0.0, u - tile_u))
                        local_v = min(1.0, max(0.0, v - tile_v))
                        atlas_u = (px + 0.5 + local_u * (texture_width - 1)) / atlas_width
                        atlas_v = (py + 0.5 + (1.0 - local_v) * (texture_height - 1)) / atlas_height
                    records += struct.pack("<ffIfff", atlas_u, atlas_v, 0xFFFFFFFF, x, y, z)
                    vertex_count += 1
    magic = (b"MKA4" if direct_has_alpha else b"MKO4") if direct_texture else b"MKT2"
    output = struct.pack("<4sIII", magic, vertex_count, 24,
                         (atlas_width << 16) | atlas_height) + records
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(output)
    bounds = (
        (min(xs) - center_x) * scale, (max(xs) - center_x) * scale,
        0.0, (max(ys) - floor_y) * scale,
        (min(zs) - center_z) * scale, (max(zs) - center_z) * scale,
    )
    return vertex_count, bounds


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--scale", type=float, default=0.25)
    parser.add_argument("--mtl", type=Path, required=True)
    parser.add_argument("--texture-output", type=Path, required=True)
    parser.add_argument("--only-materials", nargs="*", default=None)
    parser.add_argument("--no-tile", action="store_true", help="keep the original low-poly geometry")
    parser.add_argument("--direct-texture", action="store_true", help="use one repeatable texture without an atlas")
    parser.add_argument("--max-edge", type=float, default=3.0,
                        help="maximum world-space triangle edge for direct textures")
    args = parser.parse_args()
    count, bounds = convert(args.source, args.destination, args.scale, args.mtl, args.texture_output,
                            set(args.only_materials) if args.only_materials else None, args.no_tile,
                            args.direct_texture, args.max_edge)
    print(f"Wrote {count} vertices to {args.destination}")
    print("Bounds x/y/z:", bounds[0:2], bounds[2:4], bounds[4:6])


if __name__ == "__main__":
    main()
