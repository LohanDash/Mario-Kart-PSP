#!/usr/bin/env python3
"""Decompress a Mario Kart DS CARC file and extract its NARC archive."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def decompress_lz10(data: bytes) -> bytes:
    if len(data) < 4 or data[0] != 0x10:
        raise ValueError("not a Nintendo DS LZ10 stream")
    expected = int.from_bytes(data[1:4], "little")
    source = 4
    output = bytearray()
    while len(output) < expected:
        if source >= len(data):
            raise ValueError("truncated LZ10 flag byte")
        flags = data[source]
        source += 1
        for bit in range(7, -1, -1):
            if len(output) >= expected:
                break
            if flags & (1 << bit):
                if source + 2 > len(data):
                    raise ValueError("truncated LZ10 back-reference")
                pair = (data[source] << 8) | data[source + 1]
                source += 2
                length = (pair >> 12) + 3
                distance = (pair & 0xFFF) + 1
                if distance > len(output) or len(output) + length > expected:
                    raise ValueError("invalid LZ10 back-reference")
                for _ in range(length):
                    output.append(output[-distance])
            else:
                if source >= len(data):
                    raise ValueError("truncated LZ10 literal")
                output.append(data[source])
                source += 1
    return bytes(output)


def safe_name(raw: bytes) -> str:
    name = raw.decode("ascii", errors="replace")
    if not name or name in {".", ".."} or "/" in name or "\\" in name:
        raise ValueError(f"unsafe archive name: {name!r}")
    return name


def read_blocks(narc: bytes) -> dict[bytes, bytes]:
    if len(narc) < 16 or narc[:4] != b"NARC":
        raise ValueError("decompressed data is not a NARC archive")
    file_size = u32(narc, 8)
    header_size = u16(narc, 12)
    block_count = u16(narc, 14)
    if file_size > len(narc) or header_size < 16:
        raise ValueError("invalid NARC header")
    blocks: dict[bytes, bytes] = {}
    cursor = header_size
    for _ in range(block_count):
        if cursor + 8 > file_size:
            raise ValueError("truncated NARC block")
        magic = narc[cursor:cursor + 4]
        size = u32(narc, cursor + 4)
        if size < 8 or cursor + size > file_size:
            raise ValueError("invalid NARC block size")
        blocks[magic] = narc[cursor + 8:cursor + size]
        cursor += size
    return blocks


def extract_narc(narc: bytes, destination: Path) -> int:
    blocks = read_blocks(narc)
    try:
        fat, fnt, image = blocks[b"BTAF"], blocks[b"BTNF"], blocks[b"GMIF"]
    except KeyError as error:
        raise ValueError(f"missing NARC block {error.args[0]!r}") from error
    file_count = u16(fat, 0)
    if 4 + file_count * 8 > len(fat):
        raise ValueError("truncated NARC file allocation table")
    entries = [(u32(fat, 4 + i * 8), u32(fat, 8 + i * 8)) for i in range(file_count)]
    destination.mkdir(parents=True, exist_ok=True)
    written = 0

    def visit(directory_id: int, output: Path) -> None:
        nonlocal written
        index = directory_id - 0xF000
        if index < 0 or index * 8 + 8 > len(fnt):
            raise ValueError(f"invalid directory id {directory_id:#x}")
        cursor = u32(fnt, index * 8)
        file_id = u16(fnt, index * 8 + 4)
        output.mkdir(parents=True, exist_ok=True)
        while cursor < len(fnt):
            marker = fnt[cursor]
            cursor += 1
            if marker == 0:
                return
            length = marker & 0x7F
            if cursor + length > len(fnt):
                raise ValueError("truncated archive filename")
            name = safe_name(fnt[cursor:cursor + length])
            cursor += length
            if marker & 0x80:
                if cursor + 2 > len(fnt):
                    raise ValueError("truncated archive directory")
                child_id = u16(fnt, cursor)
                cursor += 2
                visit(child_id, output / name)
            else:
                if file_id >= file_count:
                    raise ValueError("file id exceeds NARC allocation table")
                start, end = entries[file_id]
                if start > end or end > len(image):
                    raise ValueError(f"invalid NARC entry for {name}")
                (output / name).write_bytes(image[start:end])
                file_id += 1
                written += 1
        raise ValueError("unterminated archive directory")

    visit(0xF000, destination)
    return written


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    narc = decompress_lz10(args.archive.read_bytes())
    count = extract_narc(narc, args.destination)
    print(f"Extracted {count} files to {args.destination}")


if __name__ == "__main__":
    main()
