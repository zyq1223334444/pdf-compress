"""Generate the three MSIX tile images used by shell/AppxManifest.xml.

The generated PNGs are checked in under shell/Resources, so a normal build of
shell/build_shell_menu.bat never needs this script. It exists so the assets can
be regenerated from scratch (and to document how they were produced):

    python shell/make_assets.py --out shell/Resources

Requires Pillow. If Pillow is not installed, restore shell/Resources/*.png from
version control instead (or run "python -m pip install pillow").
"""

import argparse
import os
import sys

TILE_NAMES = {
    "StoreLogo.png": 50,
    "Square150x150Logo.png": 150,
    "Square44x44Logo.png": 44,
}

# Flat PDF-ish red; matches the application icon used by the installer.
ACCENT = (178, 34, 34, 255)
TEXT = (255, 255, 255, 255)


def load_font(size):
    from PIL import ImageFont

    for name in ("arialbd.ttf", "arial.ttf", "segoeuib.ttf", "segoeui.ttf"):
        path = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", name)
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                pass
    return ImageFont.load_default()


def make_tile(size, label="PDF"):
    from PIL import Image, ImageDraw

    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([0, 0, size - 1, size - 1], radius=max(2, size // 7), fill=ACCENT)

    # Shrink the glyph until it fits comfortably inside the tile.
    font_size = int(size * 0.46)
    font = load_font(font_size)
    while font_size > 5:
        box = draw.textbbox((0, 0), label, font=font)
        if (box[2] - box[0]) <= size * 0.78 and (box[3] - box[1]) <= size * 0.55:
            break
        font_size -= 1
        font = load_font(font_size)

    box = draw.textbbox((0, 0), label, font=font)
    draw.text(((size - (box[2] - box[0])) / 2 - box[0],
               (size - (box[3] - box[1])) / 2 - box[1]),
              label, font=font, fill=TEXT)
    return image


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="directory that receives the PNGs")
    args = parser.parse_args(argv)

    try:
        import PIL  # noqa: F401
    except ImportError:
        sys.stderr.write("[ERR] Pillow is not installed for '%s'.\n" % sys.executable)
        sys.stderr.write("      Run: \"%s\" -m pip install pillow\n" % sys.executable)
        return 1

    os.makedirs(args.out, exist_ok=True)
    for name, size in TILE_NAMES.items():
        path = os.path.join(args.out, name)
        make_tile(size).save(path, "PNG")
        print("[+] %s (%dx%d, %d bytes)" % (path, size, size, os.path.getsize(path)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
