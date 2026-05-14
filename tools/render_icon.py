"""Render icon.svg into all platform-required sizes/formats.

Generates:
  - PNGs at 16, 32, 48, 64, 128, 192, 256, 512, 1024
  - Windows .ico (multi-resolution: 16, 32, 48, 64, 128, 256)
  - Android adaptive-icon foreground vector (separately committed)

Usage:
    python tools/render_icon.py
"""
from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtCore import Qt, QSize, QByteArray
from PySide6.QtGui import QImage, QPainter, QGuiApplication
from PySide6.QtSvg import QSvgRenderer
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SVG_PATH = ROOT / "resources" / "icon.svg"
OUT_DIR = ROOT / "resources" / "icons"
OUT_DIR.mkdir(exist_ok=True)

PNG_SIZES = [16, 32, 48, 64, 128, 192, 256, 512, 1024]
ICO_SIZES = [16, 32, 48, 64, 128, 256]


def render_png(svg_bytes: bytes, size: int) -> Path:
    renderer = QSvgRenderer(QByteArray(svg_bytes))
    image = QImage(QSize(size, size), QImage.Format.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)
    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
    renderer.render(painter)
    painter.end()
    out = OUT_DIR / f"icon_{size}.png"
    image.save(str(out), "PNG")
    return out


def main() -> int:
    _ = QGuiApplication(sys.argv)

    if not SVG_PATH.exists():
        print(f"missing {SVG_PATH}", file=sys.stderr)
        return 1

    svg_bytes = SVG_PATH.read_bytes()

    pngs = [render_png(svg_bytes, s) for s in PNG_SIZES]
    print(f"wrote {len(pngs)} PNGs to {OUT_DIR}")

    # Build a multi-resolution .ico via Pillow
    ico_path = OUT_DIR / "icon.ico"
    images = [Image.open(OUT_DIR / f"icon_{s}.png") for s in ICO_SIZES]
    images[0].save(
        str(ico_path),
        format="ICO",
        sizes=[(s, s) for s in ICO_SIZES],
        append_images=images[1:],
    )
    print(f"wrote {ico_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
