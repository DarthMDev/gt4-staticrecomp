"""TASK-086 step 1 (slice232): the CRT/SMODE gate and the frame checks of a headless run.

A headless donor run (gt4vulkan.exe with --frames-dir, stderr filtered to the [gs], [aspect] and [progressive] lines,
as private/slice232-scripts/run-headless-232.sh writes it) records:
  [gs] SetGsCrt interlace I mode 0xM field F        every CRT mode change the game makes (SetGsCrt syscall); SMODE1/2
                                                    follow it (the game writes SMODE2 from the same row every frame)
  [aspect] display request at vblank N: pending P mode A row R
                                                    func_001074D0 request samples (psm A, CRT table row R)
  [progressive] display words 0x00007F08 at vblank N: menu psm A, menu row R, ..., boot layout B, monitor row M
                                                    the host fill of the TASK-086 words

Commands:
  check --run DIR --expect on|off [--boot]   the gate:
      on:  the words ask for psm 2 / row 6 / monitor row 6 (boot layout 0, or 1 with --boot); the CRT switches to 480p
           (interlace 0, mode 0x50) at the first scene (the race start; with --boot within BOOT_VBLANKS of the boot)
           and never changes again (no 1-second black between races and the menus after them); every request after
           the first row-6 request is row 6 with psm 2 (menus and the normal race index ask for the same tuple); no
           request with psm 0 (words read before the host filled them); the frames after the switch are full 448-line
           frames (not line-doubled fields);
      off: the words hold the original constants (psm 1 / row 0 / boot layout 0 / monitor row 1); the CRT never
           leaves 480i and no request asks for row 6; the frames are line-doubled 224-line fields as in slice218.
  compare --run DIR --reference DIR  frame dumps byte for byte (same names); prints identical / different counts.

Frame measure: a 480i field is shown line-doubled in the 640x448 dump, so rows 2k and 2k+1 are identical. For each
dump, `doubled` = the fraction of row pairs that are identical among the pairs whose first row is not one flat
colour (flat rows say nothing). A frame with fewer than MIN_INFORMATIVE_PAIRS such pairs is not counted.
"""

import argparse
import json
import re
import sys
from pathlib import Path

SET_GS_CRT = re.compile(r'\[gs\] SetGsCrt interlace (\d+) mode 0x([0-9A-Fa-f]+) field (\d+)')
REQUEST = re.compile(r'\[aspect\] display request at vblank (\d+): pending (\d+) mode (\d+) row (\d+)')
WORDS = re.compile(r'\[progressive\] display words 0x([0-9A-Fa-f]+) at vblank \d+: menu psm (\d+), menu row (\d+), '
                   r'field halving 0x([0-9A-Fa-f]+), draw-back viewport (\d+), boot layout (\d+), '
                   r'monitor row (\d+)')
FRAME_NAME = re.compile(r'frame_(\d+)\.ppm$')

PROGRESSIVE_CRT = (0, 0x50, 1)
FIELD_CRT = (1, 0x02, 1)
# The 480p boot path: the display state shows row 6 within this many VBlanks (the boot reset's SetGsCrt comes first).
BOOT_VBLANKS = 60
MIN_INFORMATIVE_PAIRS = 40
# Frames counted from this many VBlanks after the first 480p request (the game's 60-frame CRT-change blank, plus margin).
SETTLE_VBLANKS = 120
FULL_FRAME_MAX_DOUBLED = 0.5    # a progressive frame: at most half of the informative row pairs repeat
FIELD_MIN_DOUBLED = 0.999       # a 480i field: every informative pair repeats
FULL_FRAME_MIN_SHARE = 0.9      # on: at least this share of the informative frames after the switch are full frames


def read_ppm(path):
    data = Path(path).read_bytes()
    if not data.startswith(b'P6'):
        raise ValueError(f'{path}: not a binary PPM')
    fields = []
    position = 2
    while len(fields) < 3:
        while data[position:position + 1].isspace():
            position += 1
        if data[position:position + 1] == b'#':
            position = data.index(b'\n', position) + 1
            continue
        end = position
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(int(data[position:end]))
        position = end
    width, height, maximum = fields
    if maximum != 255:
        raise ValueError(f'{path}: only 8-bit PPM is supported')
    pixels = data[position + 1:]
    if len(pixels) != width * height * 3:
        raise ValueError(f'{path}: pixel data is {len(pixels)} bytes, expected {width * height * 3}')
    return width, height, pixels


def doubled_fraction(width, height, pixels):
    """(fraction of informative row pairs that repeat, informative pair count)."""
    stride = width * 3
    informative = repeated = 0
    for pair in range(height // 2):
        first = pixels[2 * pair * stride:(2 * pair + 1) * stride]
        second = pixels[(2 * pair + 1) * stride:(2 * pair + 2) * stride]
        if first == first[:3] * width:
            continue
        informative += 1
        if first == second:
            repeated += 1
    return (repeated / informative if informative else 1.0), informative


def parse_log(text):
    crt = [tuple(int(value, 16) if index == 1 else int(value) for index, value in enumerate(match.groups()))
           for match in SET_GS_CRT.finditer(text)]
    requests = [{'vblank': int(m[1]), 'pending': int(m[2]), 'psm': int(m[3]), 'row': int(m[4])}
                for m in REQUEST.finditer(text)]
    words = [{'block': int(m[1], 16), 'psm': int(m[2]), 'row': int(m[3]), 'halving': int(m[4], 16),
              'viewport': int(m[5]), 'boot_layout': int(m[6]), 'monitor_row': int(m[7])} for m in WORDS.finditer(text)]
    return {'crt': crt, 'requests': requests, 'words': words}


def frame_measures(frames_dir):
    measures = []
    for path in sorted(Path(frames_dir).glob('frame_*.ppm')):
        match = FRAME_NAME.search(path.name)
        if not match:
            continue
        width, height, pixels = read_ppm(path)
        fraction, informative = doubled_fraction(width, height, pixels)
        measures.append({'frame': int(match.group(1)), 'width': width, 'height': height,
                         'doubled': round(fraction, 4), 'informative_pairs': informative})
    return measures


def check(run, expect, boot=False):
    """The gate. `boot` (the experimental GT4_PROGRESSIVE_BOOT=1 path) also requires boot layout 1 and the 480p
    display state within BOOT_VBLANKS; without it the boot layout word must be 0 and the switch comes with the first
    scene (race start)."""
    run = Path(run)
    log = parse_log((run / 'stderr-filtered.log').read_text(encoding='utf-8', errors='replace'))
    frames = frame_measures(run / 'frames')
    failures = []
    if not log['words']:
        failures.append('no [progressive] display words line (not a progressive-library build?)')
    if any(request['psm'] == 0 for request in log['requests']):
        failures.append('a display request with psm 0 (the words were read before the host filled them)')
    sizes = {(frame['width'], frame['height']) for frame in frames}
    if sizes - {(640, 448)}:
        failures.append(f'frame sizes {sorted(sizes)}')
    informative = [frame for frame in frames if frame['informative_pairs'] >= MIN_INFORMATIVE_PAIRS]
    summary = {'run': str(run), 'expect': expect, 'crt_changes': [list(item) for item in log['crt']],
               'requests': log['requests'], 'words': log['words'], 'frames': len(frames)}
    if expect == 'on':
        wanted = (2, 6, 1 if boot else 0, 6)
        if log['words'] and (log['words'][-1]['psm'], log['words'][-1]['row'], log['words'][-1]['boot_layout'],
                             log['words'][-1]['monitor_row']) != wanted:
            failures.append(f"words {log['words'][-1]} are not psm / row / boot layout / monitor row {wanted}")
        if PROGRESSIVE_CRT not in log['crt']:
            failures.append('the CRT never switched to 480p (interlace 0, mode 0x50, field 1)')
        else:
            after = log['crt'][log['crt'].index(PROGRESSIVE_CRT) + 1:]
            if after:
                failures.append(f'CRT changes after the switch to 480p: {after}')
        first_row6 = next((request for request in log['requests'] if request['row'] == 6), None)
        if first_row6 is None:
            failures.append('no display request for CRT row 6')
            counted = []
        else:
            if boot and first_row6['vblank'] > BOOT_VBLANKS:
                failures.append(f"the boot did not switch to 480p: the first row-6 state is at VBlank {first_row6['vblank']}")
            later = log['requests'][log['requests'].index(first_row6):]
            other = [request for request in later if (request['psm'], request['row']) != (2, 6)]
            if other:
                failures.append(f'requests other than (psm 2, row 6) after the first row-6 request: {other}')
            counted = [frame for frame in informative if frame['frame'] >= first_row6['vblank'] + SETTLE_VBLANKS]
            full = [frame for frame in counted if frame['doubled'] <= FULL_FRAME_MAX_DOUBLED]
            summary['full_frames'] = f'{len(full)} of {len(counted)} informative frames after the switch'
            if not counted:
                failures.append('no informative frame after the switch')
            elif len(full) < FULL_FRAME_MIN_SHARE * len(counted):
                failures.append(f"only {summary['full_frames']} are full 448-line frames")
        summary['doubled_after_switch'] = [[frame['frame'], frame['doubled']] for frame in counted]
    else:
        if log['words'] and (log['words'][-1]['psm'], log['words'][-1]['row'], log['words'][-1]['boot_layout'],
                             log['words'][-1]['monitor_row']) != (1, 0, 0, 1):
            failures.append(f"words {log['words'][-1]} are not the original psm 1 / row 0 / boot layout 0 / monitor row 1")
        if any(item[0] == 0 for item in log['crt']):
            failures.append(f"a progressive CRT mode with the switch off: {log['crt']}")
        if any(request['row'] == 6 for request in log['requests']):
            failures.append('a request for CRT row 6 with the switch off')
        fields = [frame for frame in informative if frame['doubled'] >= FIELD_MIN_DOUBLED]
        summary['field_frames'] = f'{len(fields)} of {len(informative)} informative frames'
        if len(fields) != len(informative):
            failures.append(f"only {summary['field_frames']} are line-doubled fields")
    summary['failures'] = failures
    summary['result'] = 'ok' if not failures else 'fail'
    return summary


def compare(run, reference):
    names = sorted(path.name for path in Path(reference, 'frames').glob('frame_*.ppm'))
    ours = {path.name for path in Path(run, 'frames').glob('frame_*.ppm')}
    identical = [name for name in names if name in ours and
                 Path(run, 'frames', name).read_bytes() == Path(reference, 'frames', name).read_bytes()]
    different = [name for name in names if name in ours and name not in identical]
    missing = [name for name in names if name not in ours]
    extra = sorted(ours - set(names))
    return {'run': str(run), 'reference': str(reference), 'reference_frames': len(names),
            'identical': len(identical), 'different': different, 'missing': missing, 'extra': extra,
            'first_different': different[0] if different else None}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest='command', required=True)
    gate = commands.add_parser('check')
    gate.add_argument('--run', type=Path, required=True)
    gate.add_argument('--expect', choices=('on', 'off'), required=True)
    gate.add_argument('--boot', action='store_true', help='the run used GT4_PROGRESSIVE_BOOT=1 (experimental)')
    same = commands.add_parser('compare')
    same.add_argument('--run', type=Path, required=True)
    same.add_argument('--reference', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'check':
            summary = check(args.run, args.expect, args.boot)
            print(json.dumps(summary, indent=1))
            return 0 if summary['result'] == 'ok' else 1
        result = compare(args.run, args.reference)
        print(json.dumps(result, indent=1))
        return 0
    except (OSError, ValueError) as error:
        print(f'progressive-gate: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
