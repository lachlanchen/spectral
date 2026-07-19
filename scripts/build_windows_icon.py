"""Build a multi-resolution Windows icon from the canonical SVG artwork."""

from pathlib import Path

from PIL import Image
from PySide6 import QtCore, QtGui, QtSvg


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "spectral" / "resources" / "icon.svg"
TARGET = ROOT / "src" / "spectral" / "resources" / "icon.ico"
TEMP = TARGET.with_suffix(".icon-source.png")


def main() -> None:
    size = 256
    image = QtGui.QImage(size, size, QtGui.QImage.Format.Format_ARGB32)
    image.fill(QtCore.Qt.GlobalColor.transparent)
    painter = QtGui.QPainter(image)
    painter.setRenderHint(QtGui.QPainter.RenderHint.Antialiasing, True)
    renderer = QtSvg.QSvgRenderer(str(SOURCE))
    renderer.render(painter, QtCore.QRectF(0, 0, size, size))
    painter.end()
    if not image.save(str(TEMP), "PNG"):
        raise RuntimeError(f"Could not render {SOURCE}")
    with Image.open(TEMP) as rendered:
        rendered.convert("RGBA").save(
            TARGET,
            format="ICO",
            sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40),
                   (48, 48), (64, 64), (128, 128), (256, 256)],
        )
    TEMP.unlink()
    print(TARGET)


if __name__ == "__main__":
    main()
