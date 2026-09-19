#!/usr/bin/env python3
"""Extract the NitroFS from a Nintendo DS ROM without third-party modules."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def safe_name(raw: bytes) -> str:
    name = raw.decode("ascii", errors="replace")
    if not name or name in {".", ".."} or "/" in name or "\\" in name:
        raise ValueError(f"unsafe NitroFS name: {name!r}")
    return name


def extract(rom_path: Path, destination: Path) -> int:
    rom = rom_path.read_bytes()
    if len(rom) < 0x200 or rom[0x0C:0x10] != b"AMCP":
        raise ValueError("the file is not the supported European Mario Kart DS ROM")

    fnt_offset, fnt_size = u32(rom, 0x40), u32(rom, 0x44)
    fat_offset, fat_size = u32(rom, 0x48), u32(rom, 0x4C)
    if fnt_offset + fnt_size > len(rom) or fat_offset + fat_size > len(rom):
        raise ValueError("invalid NitroFS tables")

    fnt = rom[fnt_offset:fnt_offset + fnt_size]
    file_count = fat_size // 8
    dir_count = u16(fnt, 6)
    destination.mkdir(parents=True, exist_ok=True)
    written = 0

    def visit(directory_id: int, output: Path) -> None:
        nonlocal written
        index = directory_id - 0xF000
        if index < 0 or index >= dir_count:
            raise ValueError(f"invalid directory id {directory_id:#x}")
        subtable = u32(fnt, index * 8)
        file_id = u16(fnt, index * 8 + 4)
        output.mkdir(parents=True, exist_ok=True)
        cursor = subtable
        while cursor < len(fnt):
            marker = fnt[cursor]
            cursor += 1
            if marker == 0:
                return
            length = marker & 0x7F
            is_directory = bool(marker & 0x80)
            name = safe_name(fnt[cursor:cursor + length])
            cursor += length
            if is_directory:
                child_id = u16(fnt, cursor)
                cursor += 2
                visit(child_id, output / name)
            else:
                if file_id >= file_count:
                    raise ValueError("file id exceeds FAT")
                start = u32(rom, fat_offset + file_id * 8)
                end = u32(rom, fat_offset + file_id * 8 + 4)
                if start > end or end > len(rom):
                    raise ValueError(f"invalid FAT entry for {name}")
                (output / name).write_bytes(rom[start:end])
                file_id += 1
                written += 1
        raise ValueError("unterminated NitroFS directory")

    visit(0xF000, destination)
    return written


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    count = extract(args.rom, args.destination)
    print(f"Extracted {count} files to {args.destination}")


if __name__ == "__main__":
    main()

