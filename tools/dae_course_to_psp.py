#!/usr/bin/env python3
'''Convert an Apicula course DAE into per-material PSP streams and collision.'''

import argparse
import math
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image
from obj_to_psp import clip_polygon, spatial_triangles, swizzle_rgba


MIRRORED_MATERIALS = {6, 14, 25}


def swizzle_16(data, width, height):
    """Arrange 16-bit pixels in the PSP GE's 16-byte by 8-row tiles."""
    row_bytes = width * 2
    if row_bytes % 16 or height % 8:
        raise ValueError('swizzled 16-bit PSP textures require 16-byte rows and 8-row height')
    output = bytearray()
    for y in range(0, height, 8):
        for x in range(0, row_bytes, 16):
            for row in range(8):
                start = (y + row) * row_bytes + x
                output += data[start:start + 16]
    return bytes(output)


def encode_16(image, alpha):
    output = bytearray()
    for red, green, blue, opacity in image.getdata():
        if alpha:
            pixel = ((red >> 3) | ((green >> 3) << 5) |
                     ((blue >> 3) << 10) | ((opacity >= 128) << 15))
        else:
            pixel = (red >> 3) | ((green >> 2) << 5) | ((blue >> 3) << 11)
        output += struct.pack('<H', pixel)
    return swizzle_16(output, image.width, image.height)


def child(element, suffix):
    return next(item for item in element.iter() if item.tag.endswith(suffix))


def optional_text(element, suffix, default):
    item = next((item for item in element.iter() if item.tag.endswith(suffix)), None)
    if item is None or not item.text:
        return default
    return item.text.strip().upper()


def mirrored_triangles(triangle, mirror_s, mirror_t):
    polygons = [(0, 0, list(triangle))]
    for axis, enabled in ((0, mirror_s), (1, mirror_t)):
        if not enabled:
            continue
        divided = []
        for tile_u, tile_v, polygon in polygons:
            first = math.floor(min(vertex[axis] for vertex in polygon))
            last = math.ceil(max(vertex[axis] for vertex in polygon)) - 1
            if last < first:
                last = first
            for tile in range(first, last + 1):
                clipped = clip_polygon(polygon, axis, tile, True)
                if clipped:
                    clipped = clip_polygon(clipped, axis, tile + 1, False)
                if len(clipped) >= 3:
                    divided.append((tile if axis == 0 else tile_u,
                                    tile if axis == 1 else tile_v, clipped))
        polygons = divided
    for tile_u, tile_v, polygon in polygons:
        mapped = []
        for u, v, x, y, z in polygon:
            if mirror_s:
                u = min(1.0, max(0.0, u - tile_u))
                if tile_u % 2:
                    u = 1.0 - u
            if mirror_t:
                v = min(1.0, max(0.0, v - tile_v))
                if tile_v % 2:
                    v = 1.0 - v
            mapped.append((u, 1.0 - v, x, y, z))
        for index in range(1, len(mapped) - 1):
            yield mapped[0], mapped[index], mapped[index + 1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    parser.add_argument('--scale', type=float, default=0.25)
    parser.add_argument('--max-edge', type=float, default=6.0,
                        help='Maximum rendered triangle edge; 6 keeps PSP draw cost low')
    parser.add_argument('--preserve-origin', action='store_true',
                        help='Keep model-local coordinates for placed MapObj models')
    parser.add_argument('--texture-format', choices=('32', '16'), default='32',
                        help='PSP texture depth; 16 halves texture bandwidth')
    args = parser.parse_args()

    root = ET.parse(args.source).getroot()
    images = {}
    materials = {}
    effects = {}
    wrap_modes = {}
    for item in root.iter():
        if item.tag.endswith('image') and 'id' in item.attrib:
            images[item.attrib['id']] = child(item, 'init_from').text
        elif item.tag.endswith('effect') and 'id' in item.attrib:
            effect_id = item.attrib['id']
            effects[effect_id] = child(item, 'init_from').text
            wrap_modes[effect_id] = (
                optional_text(item, 'wrap_s', 'WRAP'),
                optional_text(item, 'wrap_t', 'WRAP'),
            )
        elif item.tag.endswith('material') and 'id' in item.attrib:
            instance = child(item, 'instance_effect')
            materials[item.attrib['id']] = instance.attrib['url'].lstrip('#')

    position_source = next(x for x in root.iter()
                           if x.tag.endswith('source') and x.attrib.get('id') == 'positions')
    texcoord_source = next(x for x in root.iter()
                           if x.tag.endswith('source') and x.attrib.get('id') == 'texcoords')
    position_data = list(map(float, child(position_source, 'float_array').text.split()))
    texcoord_data = list(map(float, child(texcoord_source, 'float_array').text.split()))
    positions = list(zip(position_data[::3], position_data[1::3], position_data[2::3]))
    texcoords = list(zip(texcoord_data[::2], texcoord_data[1::2]))
    xs, ys, zs = zip(*positions)
    center_x = (min(xs) + max(xs)) * 0.5
    center_z = (min(zs) + max(zs)) * 0.5
    floor_y = min(ys)
    if args.preserve_origin:
        transformed = [(x * args.scale, y * args.scale, z * args.scale)
                       for x, y, z in positions]
    else:
        transformed = [((x - center_x) * args.scale, (y - floor_y) * args.scale,
                        (z - center_z) * args.scale) for x, y, z in positions]

    per_material = {}
    collision = bytearray()
    for polylist in (x for x in root.iter() if x.tag.endswith('polylist')):
        material_id = polylist.attrib['material']
        material_number = int(material_id.replace('material', ''))
        effect_id = materials[material_id]
        wrap_s, wrap_t = wrap_modes.get(effect_id, ('WRAP', 'WRAP'))
        mirror_s = wrap_s == 'MIRROR' and material_number in MIRRORED_MATERIALS
        mirror_t = wrap_t == 'MIRROR' and material_number in MIRRORED_MATERIALS
        counts = list(map(int, child(polylist, 'vcount').text.split()))
        indices = list(map(int, child(polylist, 'p').text.split()))
        output = per_material.setdefault(material_id, bytearray())
        cursor = 0
        for count in counts:
            polygon = indices[cursor:cursor + count]
            cursor += count
            for index in range(1, count - 1):
                triangle = []
                for vertex_index in (polygon[0], polygon[index], polygon[index + 1]):
                    u, v = texcoords[vertex_index]
                    x, y, z = transformed[vertex_index]
                    triangle.append((u, v, x, y, z))
                    collision += struct.pack('<ffIfff', 0.0, 0.0, 0xFFFFFFFF, x, y, z)
                for mirrored in mirrored_triangles(tuple(triangle), mirror_s, mirror_t):
                    for part in spatial_triangles(mirrored, args.max_edge):
                        for u, v, x, y, z in part:
                            output += struct.pack('<ffIfff', u, v, 0xFFFFFFFF, x, y, z)

    args.destination.mkdir(parents=True, exist_ok=True)
    for material_id, vertices in per_material.items():
        number = int(material_id.replace('material', ''))
        effect_id = materials[material_id]
        image_id = effects[effect_id]
        image_path = args.source.parent / images[image_id]
        image = Image.open(image_path).convert('RGBA')
        wrap_s, wrap_t = wrap_modes.get(effect_id, ('WRAP', 'WRAP'))
        mirror_s = wrap_s == 'MIRROR' and number in MIRRORED_MATERIALS
        mirror_t = wrap_t == 'MIRROR' and number in MIRRORED_MATERIALS
        width, height = image.size
        rgba = image.tobytes()
        alpha = image.getchannel('A').getextrema()[0] < 255
        if args.texture_format == '16':
            magic = b'MK51' if alpha else b'MK65'
            texture_data = encode_16(image, alpha)
        else:
            magic = b'MKA4' if alpha else b'MKO4'
            texture_data = swizzle_rgba(rgba, width, height)
        stem = args.destination / f'course_mat_{number}'
        stem.with_suffix('.mkt').write_bytes(struct.pack(
            '<4sIII', magic, len(vertices) // 24, 24, (width << 16) | height) + vertices)
        stem.with_suffix('.rgba').write_bytes(texture_data)
        print(number, len(vertices) // 24, image_path.name,
              'mirror_s' if mirror_s else 'wrap_s',
              'mirror_t' if mirror_t else 'wrap_t',
              'alpha' if alpha else 'opaque')

    (args.destination / 'collision.mkt').write_bytes(struct.pack(
        '<4sIII', b'MKT2', len(collision) // 24, 24, 0) + collision)
    print('bounds', min(xs), max(xs), floor_y, max(ys), min(zs), max(zs))


if __name__ == '__main__':
    main()
