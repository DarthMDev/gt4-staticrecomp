"""Makes the white-text variant of the GT4 logo for the launcher's dark backgrounds (TASK-019).

The source logo has black wordmark text ("GRAN TURISMO 4", "THE REAL DRIVING SIMULATOR") and the GT emblem in
blue and red. The variant turns only the near-black, achromatic pixels white. Coloured pixels (the emblem) are kept,
and so is the alpha channel. The blend is by luminance and chroma with smoothstep ramps, not a hard threshold, so the
anti-aliased edges of the letters turn white smoothly.

Run from the repository root (the script is repeatable: same input, same output bytes):
  <python with pillow> -I launcher/tools/make_white_logo.py
"""

import argparse
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = ROOT / "launcher" / "assets" / "brand" / "logo-gt4.png"
DEFAULT_OUTPUT = ROOT / "launcher" / "crates" / "gt4l-tauri" / "ui" / "assets" / "logo-gt4-white.png"

# Luminance ramp (0-255): at or below DARK_FULL a pixel counts as fully dark, at or above DARK_NONE not at all.
DARK_FULL = 60.0
DARK_NONE = 140.0
# Chroma ramp: below GREY_FULL a pixel is achromatic (black or grey text), above GREY_NONE it is coloured (emblem).
GREY_FULL = 30.0
GREY_NONE = 90.0
WHITE = 255.0


def smoothstep(edge0, edge1, value):
    """0 at edge0, 1 at edge1, smooth in between (Hermite)."""
    if edge0 == edge1:
        return 0.0 if value < edge0 else 1.0
    t = min(1.0, max(0.0, (value - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def whiten(red, green, blue):
    """Returns the output RGB of one pixel: the weight of white is dark * achromatic."""
    luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue
    chroma = max(red, green, blue) - min(red, green, blue)
    dark = 1.0 - smoothstep(DARK_FULL, DARK_NONE, luminance)
    grey = 1.0 - smoothstep(GREY_FULL, GREY_NONE, chroma)
    weight = dark * grey
    return tuple(round(channel * (1.0 - weight) + WHITE * weight) for channel in (red, green, blue))


def make_white_logo(source, output):
    image = Image.open(source).convert("RGBA")
    pixels = image.load()
    width, height = image.size
    for y in range(height):
        for x in range(width):
            red, green, blue, alpha = pixels[x, y]
            if alpha == 0:
                continue
            new_red, new_green, new_blue = whiten(red, green, blue)
            pixels[x, y] = (new_red, new_green, new_blue, alpha)
    output.parent.mkdir(parents=True, exist_ok=True)
    image.save(output, format="PNG", optimize=True)
    return image.size


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    options = parser.parse_args()
    size = make_white_logo(options.source, options.output)
    print(f"wrote {options.output} ({size[0]}x{size[1]})")


if __name__ == "__main__":
    main()
