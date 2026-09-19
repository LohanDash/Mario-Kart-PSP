#!/usr/bin/env python3
"""Bake one skinned Apicula GLB animation into a PSP MKA5 vertex stream."""

import argparse
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/_anim_vendor"))
import numpy as np
from PIL import Image
from pygltflib import GLTF2
from obj_to_psp import swizzle_rgba

DTYPES = {5120: np.int8, 5121: np.uint8, 5122: np.int16,
          5123: np.uint16, 5125: np.uint32, 5126: np.float32}
SIZES = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--scale", type=float, default=.25)
    parser.add_argument("--fps", type=float, default=30.0)
    args = parser.parse_args()

    gltf = GLTF2().load_binary(str(args.source))
    blob = gltf.binary_blob()

    def accessor(index):
        item = gltf.accessors[index]
        view = gltf.bufferViews[item.bufferView]
        dtype = np.dtype(DTYPES[item.componentType]).newbyteorder("<")
        width = SIZES[item.type]
        offset = (view.byteOffset or 0) + (item.byteOffset or 0)
        packed = dtype.itemsize * width
        if view.byteStride and view.byteStride != packed:
            result = np.asarray([
                np.frombuffer(blob, dtype=dtype, count=width,
                              offset=offset + row * view.byteStride)
                for row in range(item.count)])
        else:
            result = np.frombuffer(blob, dtype=dtype,
                                   count=item.count * width,
                                   offset=offset).reshape(item.count, width)
        if item.normalized:
            if np.issubdtype(dtype, np.unsignedinteger):
                result = result.astype(np.float32) / np.iinfo(dtype).max
            else:
                result = np.maximum(result.astype(np.float32) /
                                    np.iinfo(dtype).max, -1.0)
        return result

    def quaternion_matrix(value):
        x, y, z, w = value / max(np.linalg.norm(value), 1e-8)
        return np.array([
            [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), 0],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), 0],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), 0],
            [0, 0, 0, 1]], dtype=np.float32)

    def local_matrix(node, overrides=None):
        if node.matrix:
            return np.array(node.matrix, dtype=np.float32).reshape(4, 4).T
        translation = np.array(node.translation or [0, 0, 0], dtype=np.float32)
        rotation = np.array(node.rotation or [0, 0, 0, 1], dtype=np.float32)
        scale = np.array(node.scale or [1, 1, 1], dtype=np.float32)
        if overrides:
            translation = overrides.get("translation", translation)
            rotation = overrides.get("rotation", rotation)
            scale = overrides.get("scale", scale)
        matrix = quaternion_matrix(rotation)
        matrix[:3, :3] *= scale[np.newaxis, :]
        matrix[:3, 3] = translation
        return matrix

    parents = {}
    for node_index, node in enumerate(gltf.nodes):
        for child in node.children or []:
            parents[child] = node_index

    def global_matrices(overrides):
        cache = {}
        def calculate(index):
            if index not in cache:
                local = local_matrix(gltf.nodes[index], overrides.get(index))
                cache[index] = calculate(parents[index]) @ local \
                    if index in parents else local
            return cache[index]
        return [calculate(i) for i in range(len(gltf.nodes))]

    if not gltf.animations or not gltf.skins:
        raise ValueError("GLB has no skinned animation")
    animation = gltf.animations[0]
    skin = gltf.skins[0]
    inverse_bind = accessor(skin.inverseBindMatrices).reshape(-1, 4, 4).transpose(0, 2, 1)
    mesh_node = next(i for i, node in enumerate(gltf.nodes) if node.mesh is not None)

    primitives = []
    used_images = []
    for primitive in gltf.meshes[gltf.nodes[mesh_node].mesh].primitives:
        attributes = primitive.attributes
        material = gltf.materials[primitive.material]
        texture = material.pbrMetallicRoughness.baseColorTexture
        image_index = gltf.textures[texture.index].source
        if image_index not in used_images:
            used_images.append(image_index)
        primitives.append((
            accessor(attributes.POSITION).astype(np.float32),
            accessor(attributes.TEXCOORD_0).astype(np.float32),
            accessor(attributes.JOINTS_0).astype(np.int32),
            accessor(attributes.WEIGHTS_0).astype(np.float32),
            accessor(primitive.indices).reshape(-1).astype(np.int32),
            image_index))

    images = {}
    for image_index in used_images:
        uri = gltf.images[image_index].uri
        if not uri:
            raise ValueError("embedded GLB images are not supported")
        images[image_index] = Image.open(args.source.parent / uri).convert("RGBA")
    atlas_width = (sum(images[i].width for i in used_images) + 3) & ~3
    atlas_height = (max(images[i].height for i in used_images) + 7) & ~7
    atlas = Image.new("RGBA", (atlas_width, atlas_height), (0, 0, 0, 0))
    offsets = {}
    cursor = 0
    for image_index in used_images:
        offsets[image_index] = cursor
        atlas.alpha_composite(images[image_index], (cursor, 0))
        cursor += images[image_index].width

    def sample(sampler, time, rotation=False):
        times = accessor(sampler.input).reshape(-1)
        values = accessor(sampler.output)
        if time <= times[0]:
            return values[0]
        if time >= times[-1]:
            return values[-1]
        high = int(np.searchsorted(times, time))
        low = high - 1
        amount = float((time - times[low]) / (times[high] - times[low]))
        first, second = values[low], values[high]
        if rotation and np.dot(first, second) < 0.0:
            second = -second
        value = first * (1.0 - amount) + second * amount
        if rotation:
            value /= max(np.linalg.norm(value), 1e-8)
        return value

    duration = max(float(accessor(s.input).reshape(-1)[-1])
                   for s in animation.samplers)
    frame_count = max(1, int(math.ceil(duration * args.fps)))
    frames = []
    vertices_per_frame = None
    for frame in range(frame_count):
        time = duration * frame / frame_count
        overrides = {}
        for channel in animation.channels:
            target = channel.target
            overrides.setdefault(target.node, {})[target.path] = sample(
                animation.samplers[channel.sampler], time,
                target.path == "rotation")
        globals_now = global_matrices(overrides)
        mesh_inverse = np.linalg.inv(globals_now[mesh_node])
        skin_matrices = [mesh_inverse @ globals_now[joint] @ inverse_bind[i]
                         for i, joint in enumerate(skin.joints)]
        records = []
        for positions, uvs, joints, weights, indices, image_index in primitives:
            transformed = []
            for vertex_index, position in enumerate(positions):
                source = np.array([*position, 1.0], dtype=np.float32)
                result = np.zeros(4, dtype=np.float32)
                for influence in range(4):
                    if weights[vertex_index, influence] != 0.0:
                        result += weights[vertex_index, influence] * (
                            skin_matrices[joints[vertex_index, influence]] @ source)
                transformed.append(result[:3])
            image = images[image_index]
            offset = offsets[image_index]
            for vertex_index in indices:
                position = transformed[vertex_index]
                u, v = uvs[vertex_index]
                atlas_u = (offset + .5 + max(0.0, min(1.0, float(u))) *
                           (image.width - 1)) / atlas_width
                atlas_v = (.5 + max(0.0, min(1.0, float(v))) *
                           (image.height - 1)) / atlas_height
                records.append(struct.pack("<ffIfff", atlas_u, atlas_v, 0xffffffff,
                    float(position[0]) * args.scale,
                    float(position[1]) * args.scale,
                    float(position[2]) * args.scale))
        vertices_per_frame = vertices_per_frame or len(records)
        if len(records) != vertices_per_frame:
            raise ValueError("animation changed vertex count")
        frames.extend(records)

    args.destination.parent.mkdir(parents=True, exist_ok=True)
    header = struct.pack("<4sIIIII", b"MKA5", frame_count,
                         vertices_per_frame, 24,
                         (atlas_width << 16) | atlas_height, frame_count)
    args.destination.write_bytes(header + b"".join(frames))
    texture_path = args.destination.with_suffix(".rgba")
    texture_path.write_bytes(swizzle_rgba(atlas.tobytes(), atlas_width, atlas_height))
    print(f"{args.source.name}: {frame_count} frames x {vertices_per_frame} vertices")


if __name__ == "__main__":
    main()
