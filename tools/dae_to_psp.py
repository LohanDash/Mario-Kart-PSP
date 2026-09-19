#!/usr/bin/env python3
"""Convert the static geometry in a simple COLLADA file to MKT2."""

import argparse
import math
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

from obj_to_psp import build_atlas


def source_values(root, namespace, source_id):
    source = root.find(f".//{{{namespace}}}source[@id='{source_id}']/{{{namespace}}}float_array")
    if source is None:
        raise ValueError(f"missing COLLADA source {source_id}")
    return list(map(float, source.text.split()))


def convert(source, destination, scale, texture_output):
    tree = ET.parse(source)
    root = tree.getroot()
    namespace = root.tag.split("}")[0][1:]
    geometry = root.find(f".//{{{namespace}}}geometry")
    mesh = geometry.find(f"{{{namespace}}}mesh")
    vertices = mesh.find(f"{{{namespace}}}vertices")
    inputs = {item.attrib["semantic"]: item.attrib["source"].lstrip("#") for item in vertices.findall(f"{{{namespace}}}input")}
    positions_raw = source_values(root, namespace, inputs["POSITION"])
    texcoords_raw = source_values(root, namespace, inputs["TEXCOORD"])
    positions = list(zip(positions_raw[0::3], positions_raw[1::3], positions_raw[2::3]))
    texcoords = list(zip(texcoords_raw[0::2], texcoords_raw[1::2]))
    xs, ys, zs = zip(*positions)
    center_x = (min(xs) + max(xs)) * 0.5
    center_z = (min(zs) + max(zs)) * 0.5
    floor_y = min(ys)
    transformed = [((x-center_x)*scale, (y-floor_y)*scale, (z-center_z)*scale) for x,y,z in positions]
    materials = {"material0": source.parent / "P_face_1.png", "material1": source.parent / "P_main.png"}
    placements, aw, ah = build_atlas(materials, texture_output)
    records = []
    for polylist in mesh.findall(f"{{{namespace}}}polylist"):
        material = polylist.attrib.get("material", "material1")
        indices = list(map(int, polylist.find(f"{{{namespace}}}p").text.split()))
        counts = list(map(int, polylist.find(f"{{{namespace}}}vcount").text.split()))
        px, py, tw, th = placements[material]
        cursor = 0
        for count in counts:
            polygon = indices[cursor:cursor+count]
            cursor += count
            for i in range(1, count-1):
                for index in (polygon[0], polygon[i], polygon[i+1]):
                    u, v = texcoords[index]
                    u -= math.floor(u); v -= math.floor(v)
                    au = (px + 0.5 + u * (tw-1)) / aw
                    av = (py + 0.5 + (1-v) * (th-1)) / ah
                    records.append(struct.pack("<ffIfff", au, av, 0xFFFFFFFF, *transformed[index]))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(struct.pack("<4sIII", b"MKT2", len(records), 24, (aw << 16) | ah) + b"".join(records))
    return len(records), (min(xs)-center_x)*scale, (max(xs)-center_x)*scale, (max(ys)-floor_y)*scale


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--scale", type=float, default=0.7)
    parser.add_argument("--texture-output", type=Path, required=True)
    args = parser.parse_args()
    print("Wrote", convert(args.source, args.destination, args.scale, args.texture_output))


if __name__ == "__main__":
    main()
