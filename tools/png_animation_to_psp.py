#!/usr/bin/env python3
"""Convert extracted Nitro animation frames to swizzled PSP RGBA textures."""
from pathlib import Path
from PIL import Image
from obj_to_psp import swizzle_rgba

root = Path(__file__).resolve().parents[1]
groups = {
    root / "data/courses/waluigi_pinball/animation/flipper":
        sorted((root / "assets/courses/pinball_mapobj_animated").glob("flipper_*.png")),
    root / "data/courses/waluigi_pinball/animation/bound":
        sorted((root / "assets/courses/pinball_mapobj_animated").glob("ob_bound_*.png")),
    root / "data/courses/waluigi_pinball/animation/flag":
        sorted((root / "assets/courses/pinball_animated").glob("pin_flag_*.png")),
}
for destination, sources in groups.items():
    destination.mkdir(parents=True, exist_ok=True)
    for index, source in enumerate(sources):
        image = Image.open(source).convert("RGBA")
        output = destination / f"frame_{index}.rgba"
        output.write_bytes(swizzle_rgba(image.tobytes(), *image.size))
        print(source.name, "->", output.relative_to(root))
