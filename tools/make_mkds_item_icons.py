from pathlib import Path
import struct

from PIL import Image

from obj_to_psp import swizzle_rgba


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "data/ui/mkds_item_roulette.png"
OUTPUT = ROOT / "data/ui"


def transparent_crop(box):
    image = Image.open(SOURCE).convert("RGBA").crop(box)
    pixels = []
    for r, g, b, _ in image.getdata():
        alpha = 0 if r < 24 and g < 24 and b < 24 else 255
        pixels.append((r, g, b, alpha))
    image.putdata(pixels)
    return image


def save_icon(number, image):
    # Opaque HUD panel: this bypasses the PSP alpha-path issue which made the
    # mushrooms disappear even though their inventory and texture loaded.
    canvas = Image.new("RGBA", (64, 32), (32, 32, 32, 255))
    canvas.alpha_composite(image, ((64 - image.width) // 2, (32 - image.height) // 2))
    rgba = canvas.tobytes()
    (OUTPUT / f"item_mushroom_{number}.rgba").write_bytes(swizzle_rgba(rgba, 64, 32))
    # The normal model loader also loads UI textures. The triangle is unused.
    vertex = struct.pack("<ffIfff", 0.0, 0.0, 0xFFFFFFFF, 0.0, 0.0, 0.0)
    header = struct.pack("<4sIII", b"MKA4", 1, 24, (64 << 16) | 32)
    (OUTPUT / f"item_mushroom_{number}.mkt").write_bytes(header + vertex)


single = transparent_crop((107, 5, 139, 36))
triple = transparent_crop((188, 40, 223, 72))
double = Image.new("RGBA", (48, 31), (0, 0, 0, 0))
double.alpha_composite(single, (0, 0))
double.alpha_composite(single, (16, 0))

save_icon(1, single)
save_icon(2, double)
save_icon(3, triple)
