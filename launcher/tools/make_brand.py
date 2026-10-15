"""Builds the brand assets in launcher/assets/brand from the recorded public sources.

Inputs (already fetched, recorded in brand.toml with URL, date, size and SHA-256):
  brand/source/gt4-logo.svg      Commons "Gran Turismo 4 Logo.svg" (wordmark with the GT emblem)
  brand/source/cover-front.png   libretro-thumbnails "Gran Turismo 4 (USA, Canada) (v1.01).png"

Outputs: logo-gt4.png, emblem-gt.png, cover-front.png, game.ico, launcher.ico, and brand.toml.

Run from the repository root:  python launcher/tools/make_brand.py <dir with resvg_py installed>
Requires: pip install resvg-py pillow
"""

import datetime
import hashlib
import io
import shutil
import sys
from pathlib import Path

from PIL import Image, ImageDraw

BRAND = Path(__file__).resolve().parents[1] / "assets" / "brand"
SOURCE = BRAND / "source"
ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
# Emblem crop box in the 1024-pixel-wide logo render (left, top, right, bottom).
EMBLEM_BOX = (140, 0, 880, 400)
INK = (10, 12, 20, 255)
CARBON = (19, 22, 27, 255)
PAPER = (248, 248, 248, 255)
ACCENT = (44, 107, 237, 255)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def render_logo(width: int) -> Image.Image:
    import resvg_py

    data = resvg_py.svg_to_bytes(svg_path=str(SOURCE / "gt4-logo.svg"), width=width)
    return Image.open(io.BytesIO(bytes(data))).convert("RGBA")


def tile_icon(emblem: Image.Image, size: int, background, badge: bool) -> Image.Image:
    canvas = Image.new("RGBA", (size, size), background)
    inner = int(size * 0.66)
    mark = emblem.copy()
    mark.thumbnail((inner, inner), Image.LANCZOS)
    canvas.alpha_composite(mark, ((size - mark.width) // 2, (size - mark.height) // 2))
    if badge:
        # Small play chevron in the lower-right corner, so the launcher and game icons differ.
        radius = max(3, size // 5)
        cx, cy = size - radius - max(1, size // 16), size - radius - max(1, size // 16)
        draw = ImageDraw.Draw(canvas)
        draw.ellipse((cx - radius, cy - radius, cx + radius, cy + radius), fill=ACCENT)
        draw.polygon(
            [(cx - radius * 0.35, cy - radius * 0.5), (cx - radius * 0.35, cy + radius * 0.5), (cx + radius * 0.55, cy)],
            fill=PAPER,
        )
    return canvas


def write_ico(path: Path, emblem: Image.Image, background, badge: bool) -> None:
    images = [tile_icon(emblem, size, background, badge) for size in ICON_SIZES]
    images[-1].save(path, format="ICO", sizes=[(size, size) for size in ICON_SIZES], append_images=images[:-1])


def main() -> int:
    if not (SOURCE / "gt4-logo.svg").exists():
        print("missing launcher/assets/brand/source/gt4-logo.svg", file=sys.stderr)
        return 1
    logo = render_logo(1024)
    logo.save(BRAND / "logo-gt4.png", optimize=True)
    emblem = logo.crop(EMBLEM_BOX)
    emblem.save(BRAND / "emblem-gt.png", optimize=True)
    shutil.copyfile(SOURCE / "cover-front.png", BRAND / "cover-front.png")
    write_ico(BRAND / "game.ico", emblem, INK, badge=False)
    write_ico(BRAND / "launcher.ico", emblem, CARBON, badge=True)

    today = datetime.date.today().isoformat()
    lines = [
        "# Official Gran Turismo 4 assets bundled in the launcher (owner-authorised download).",
        "# Replace this folder with launcher/assets/brand-neutral/ for a de-branded build.",
        "# Hashes are of the files in this folder; sources are where they were fetched on the date shown.",
        "",
    ]
    entries = [
        ("source/gt4-logo.svg", "Gran Turismo 4 logo (wordmark with GT emblem), SVG source",
         "https://upload.wikimedia.org/wikipedia/commons/e/e0/Gran_Turismo_4_Logo.svg",
         "Wikimedia Commons, file page File:Gran_Turismo_4_Logo.svg, licence shown there: Public domain; "
         "upstream credit: Sony Computer Entertainment (original SVG, via brandsoftheworld.com). Owner to confirm."),
        ("source/cover-front.png", "Gran Turismo 4 (USA, Canada) cover art, 512x725 front",
         "https://raw.githubusercontent.com/libretro-thumbnails/Sony_-_PlayStation_2/master/Named_Boxarts/"
         "Gran%20Turismo%204%20(USA,%20Canada)%20(v1.01).png",
         "libretro-thumbnails repository (scan of the NTSC-U box, low resolution). Licence of the artwork is "
         "Sony's; the repository terms are not stated per file. Owner to confirm or replace with a 900 px+ scan."),
        ("logo-gt4.png", "Logo rendered 1024 px wide from source/gt4-logo.svg (resvg)", None, "Derived."),
        ("emblem-gt.png", "GT emblem cropped from logo-gt4.png", None, "Derived."),
        ("cover-front.png", "Copy of source/cover-front.png", None, "Derived."),
        ("game.ico", "Game executable icon: emblem on dark tile, 10 sizes", None, "Derived (tools/make_brand.py)."),
        ("launcher.ico", "Launcher icon: emblem on carbon with play badge, 10 sizes", None, "Derived."),
    ]
    for relative, description, url, licence in entries:
        path = BRAND / relative
        lines.append("[[asset]]")
        lines.append(f'path = "{relative}"')
        lines.append(f'description = "{description}"')
        lines.append(f"size_bytes = {path.stat().st_size}")
        lines.append(f'sha256 = "{sha256(path)}"')
        if url:
            lines.append(f'source_url = "{url}"')
            lines.append(f'retrieved = "{today}"')
        lines.append(f'licence_note = "{licence}"')
        lines.append("")
    (BRAND / "brand.toml").write_text("\n".join(lines), encoding="utf-8")
    print("brand assets written to", BRAND)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
