"""Derive the slice 186 GS compute shader from the frozen MIT donor gs.wgsl.

The derived copy differs from the donor only in sprite handling for PS2_SCALE > 1, gated by setup
flag bits that the native adapter sets only when the scale is above 1 (native/gs_prim_stats.hpp):

  bit 16 (F_SPRITE_SUB): sprite coverage tests the sub-sample position (px, py), not the integer pixel.
  bit 18 (F_NO_SNAP):    the sprite texel coordinate is not snapped to the integer pixel.

At scale 1 neither bit is set, so the shader takes the donor path. Usage:
    python scripts/derive_upscale_gs_shader.py <donor gs.wgsl> <output gs.wgsl>
"""
import argparse
import hashlib
from pathlib import Path

DONOR_GS_SHA256 = 'a6ed68e9ca53ea9b6c90307d3ffeb2b74b15c5f0a20dd776f51bc243ef80d4a5'

REPLACEMENTS = (
    (
        'const F_AEM: u32 = 32768u;\n',
        'const F_AEM: u32 = 32768u;\n'
        '// Slice 186 (scripts/derive_upscale_gs_shader.py); set by the adapter only when SCALE > 1.\n'
        'const F_SPRITE_SUB: u32 = 65536u;\n'
        'const F_NO_SNAP: u32 = 262144u;\n',
    ),
    (
        '            if (point && abs(du) >= abs(dx) * 0.999) {\n'
        '                ux = f32(x);\n'
        '            }\n'
        '            if (point && abs(dv) >= abs(dy) * 0.999) {\n'
        '                uy = f32(y);\n'
        '            }\n'
        '            let cx = f32(x);\n'
        '            let cy = f32(y);\n',
        '            let sprite_sub = SCALE > 1u && has(s, F_SPRITE_SUB);\n'
        '            let snap_texel = !(SCALE > 1u && has(s, F_NO_SNAP));\n'
        '            if (point && snap_texel && abs(du) >= abs(dx) * 0.999) {\n'
        '                ux = f32(x);\n'
        '            }\n'
        '            if (point && snap_texel && abs(dv) >= abs(dy) * 0.999) {\n'
        '                uy = f32(y);\n'
        '            }\n'
        '            let cx = select(f32(x), px, sprite_sub);\n'
        '            let cy = select(f32(y), py, sprite_sub);\n',
    ),
)


def derive(donor_path, output_path):
    donor_bytes = Path(donor_path).read_bytes()
    digest = hashlib.sha256(donor_bytes).hexdigest()
    if digest != DONOR_GS_SHA256:
        raise ValueError(f'Donor gs.wgsl differs from the qualified version: {digest}')
    # The donor file uses CRLF line endings; the anchors are written with LF.
    source = donor_bytes.decode('utf-8').replace('\r\n', '\n')
    for old, new in REPLACEMENTS:
        if source.count(old) != 1:
            raise ValueError(f'Donor gs.wgsl anchor changed: {old.splitlines()[0]!r}')
        source = source.replace(old, new)
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    Path(output_path).write_text(source, encoding='utf-8', newline='\n')
    return hashlib.sha256(source.encode('utf-8')).hexdigest()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('donor', type=Path)
    parser.add_argument('output', type=Path)
    options = parser.parse_args()
    print(derive(options.donor, options.output))
