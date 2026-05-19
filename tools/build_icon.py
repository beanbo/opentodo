#!/usr/bin/env python3
"""Build a multi-resolution Windows .ico from existing PNG icons.

Windows uses different sizes from the .ico in different contexts:
  - 16x16 / 20x20 — system tray, title bar
  - 24x24 / 32x32 — small taskbar, alt-tab thumbnails
  - 48x48 — explorer "medium" icons
  - 64x64 / 96x96 — explorer "large"
  - 128x128 / 256x256 — explorer "extra large" + jump-list

A single-size .ico (which the current 666-byte file is) renders blurry
everywhere except its native size. Embed all common sizes so each context
gets the crispest image.
"""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent
ICONS_DIR = ROOT / "resources" / "icons"
OUT = ICONS_DIR / "icon.ico"

# Use the per-size PNGs we already have when available; downsample from
# the largest for the missing sizes (24). PIL Image.save(format='ICO',
# sizes=[...]) packs a multi-resolution container.
SIZES = [16, 24, 32, 48, 64, 128, 256]

# Pick the largest PNG as the master source so the few sizes we'll
# downsample (only 24) still come from a crisp original.
master = Image.open(ICONS_DIR / "icon_512.png").convert("RGBA")

# Load per-size sources where they exist; fall back to high-quality
# Lanczos downsample of the master for missing ones.
images = []
for size in SIZES:
    p = ICONS_DIR / f"icon_{size}.png"
    if p.exists():
        img = Image.open(p).convert("RGBA")
        if img.size != (size, size):
            img = img.resize((size, size), Image.LANCZOS)
    else:
        img = master.resize((size, size), Image.LANCZOS)
    images.append(img)

# Pillow's ICO writer requires sizes <= the first image. Order largest
# first so the writer accepts everything; it builds the multi-image
# container correctly.
images.sort(key=lambda im: im.size[0], reverse=True)

# Save: the first image plus `append_images` is the most reliable
# multi-size .ico recipe across Pillow versions.
images[0].save(
    OUT,
    format="ICO",
    sizes=[img.size for img in images],
    append_images=images[1:],
)

print(f"Wrote {OUT} ({OUT.stat().st_size:,} bytes, {len(images)} sizes: "
      f"{sorted([im.size[0] for im in images])})")
