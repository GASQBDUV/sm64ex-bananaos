#!/usr/bin/env python3
"""Packs the bottom-screen pictures of the SM64 3DS port "Ultimate" (maps, HUD sprites, music titles) into one file
that the bananaOS second-screen code loads at run time.

  usage: python3 -I make_minimap_pack.py <path to a clone of Epic0522/Super-Mario-64-3ds-port---Ultimate> <out minimap.pack>

The pictures are not part of this repository (the 3DS port's repository has no licence file): you make the pack from your
own clone, and it stays on your device.

Pack format (little endian):
  "BMMP", u32 map_count, u32 sprite_count
  map:    u8 level_index, u8 area_index, u32 colour (0xRRGGBB), 256*256*4 bytes RGBA
  sprite: char name[32] (without .png), u16 w, u16 h, w*h*4 bytes RGBA
level_index/area_index are the ones of src/minimap/minimap.c in that repository.
"""
import re
import struct
import sys
from pathlib import Path

from PIL import Image

if len(sys.argv) != 3:
    sys.exit(__doc__)
root = Path(sys.argv[1])
out = Path(sys.argv[2])
tex = root / "src/minimap/textures"
src = (root / "src/minimap/minimap_levels.c").read_text()

# level_xxx[] = { {name_t3x, name_t3x_size, 0xCOLOUR, placeholder}, ... };
arrays = {}
for m in re.finditer(r"const struct MiniMapInfo (level_\w+)\[\] = \{(.*?)\n\};", src, re.S):
    entries = re.findall(r"\{(\w+?)_t3x,\s*\w+,\s*(0x[0-9A-Fa-f]+),\s*(true|false)\}", m.group(2))
    arrays[m.group(1)] = entries
order = re.findall(r"MINIMAP_LEVEL\((level_\w+)\)", src)

maps = []
for li, name in enumerate(order):
    for ai, (png, colour, placeholder) in enumerate(arrays[name]):
        if placeholder == "true":
            continue
        maps.append((li, ai, int(colour, 16), tex / (png + ".png")))

sprites = sorted(p for p in tex.glob("*.png") if re.match(r"(hud_|music_)", p.name))

with out.open("wb") as f:
    f.write(b"BMMP" + struct.pack("<II", len(maps), len(sprites)))
    for li, ai, colour, path in maps:
        im = Image.open(path).convert("RGBA")
        if im.size != (256, 256):
            im = im.resize((256, 256))
        f.write(struct.pack("<BBI", li, ai, colour) + im.tobytes())
    for path in sprites:
        im = Image.open(path).convert("RGBA")
        f.write(path.stem.encode().ljust(32, b"\0")[:32] + struct.pack("<HH", *im.size) + im.tobytes())
print(f"{len(maps)} maps, {len(sprites)} sprites -> {out} ({out.stat().st_size // 1024} KB)")
