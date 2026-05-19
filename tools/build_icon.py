#!/usr/bin/env python3
"""Build a multi-resolution Windows .ico from the master SVG.

Renders directly from resources/icon.svg via Qt's QSvgRenderer (the
same engine the app itself uses) so what ships in the .exe matches
what the QML side renders. No bicubic/Lanczos downsampling artefacts
that hand-generated PNG ladders sometimes carry.

The .ico embeds these sizes so every Windows context (system tray /
taskbar / alt-tab / Explorer "small | medium | large | extra large" /
installer chrome / jump-list) gets a native-rendered, crisp image:

   16  -> tray icon, title bar
   20  -> taskbar at 125% DPI
   24  -> small taskbar
   32  -> standard taskbar
   40  -> taskbar at 150% DPI
   48  -> Explorer "medium" / large taskbar
   64  -> Explorer "large"
   96  -> Explorer at high DPI
   128 -> Explorer "extra large"
   256 -> jump-list / Cortana / extracted thumbnails

The per-size PNGs under resources/icons/ are ALSO regenerated so any
QML / QRC reference (":/icons/icons/icon_48.png" etc.) picks up the
same crisp render the .exe embeds.
"""
from pathlib import Path
import io
import sys

from PIL import Image as PILImage
from PySide6.QtCore import QBuffer, QByteArray, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

# tools/build_icon.py -> project root is one level up.
ROOT = Path(__file__).resolve().parent.parent
SVG = ROOT / "resources" / "icon.svg"
ICONS_DIR = ROOT / "resources" / "icons"
ICO = ICONS_DIR / "icon.ico"

# Bare-minimum QGuiApplication so QImage / QPainter work off-screen.
_app = QGuiApplication.instance() or QGuiApplication(sys.argv)

ICO_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
PNG_SIZES_KEEP = [16, 32, 48, 64, 128, 192, 256, 512, 1024]

renderer = QSvgRenderer(str(SVG))
if not renderer.isValid():
    sys.exit(f"QSvgRenderer rejected {SVG} — invalid SVG?")


def render(size: int) -> QImage:
    """Render the SVG at exact size x size with premultiplied alpha so
    transparency composites correctly when the .ico is alpha-blended
    over Windows' taskbar / Explorer backgrounds."""
    img = QImage(size, size, QImage.Format_ARGB32_Premultiplied)
    img.fill(Qt.transparent)
    p = QPainter(img)
    p.setRenderHint(QPainter.Antialiasing, True)
    p.setRenderHint(QPainter.SmoothPixmapTransform, True)
    renderer.render(p)
    p.end()
    return img


def qimage_to_pil(qimg: QImage) -> "PILImage.Image":
    """Convert via PNG-in-memory. Cheap for <=256x256. QImage.save needs
    a Qt IO device (QBuffer), not a Python BytesIO — so wrap the bytes
    in QBuffer first, then hand the resulting QByteArray to Pillow."""
    ba = QByteArray()
    qbuf = QBuffer(ba)
    qbuf.open(QBuffer.WriteOnly)
    qimg.save(qbuf, "PNG")
    qbuf.close()
    return PILImage.open(io.BytesIO(bytes(ba))).convert("RGBA")


# 1) Regenerate per-size PNGs (overwrites existing).
print("Per-size PNGs (from SVG via QSvgRenderer):")
for size in PNG_SIZES_KEEP:
    img = render(size)
    out = ICONS_DIR / f"icon_{size}.png"
    img.save(str(out), "PNG")
    print(f"  PNG   {out.name:18} {size}x{size:<4} {out.stat().st_size:>7,} bytes")

# 2) Build the multi-image .ico. Each size is independently SVG-rendered
#    (NOT downsampled from a master PNG), so 32x32 / 48x48 etc. land at
#    their native crispness.
print()
print("ICO container:")
ico_pils = [qimage_to_pil(render(s)) for s in ICO_SIZES]
ico_pils.sort(key=lambda im: im.size[0], reverse=True)  # largest first

ico_pils[0].save(
    str(ICO),
    format="ICO",
    sizes=[im.size for im in ico_pils],
    append_images=ico_pils[1:],
)
print(f"  ICO   {ICO.name} {ICO.stat().st_size:,} bytes — "
      f"{len(ico_pils)} sizes: {sorted(im.size[0] for im in ico_pils)}")
