"""Open the qualified Windows pilot with a persistent private memory card."""
from datetime import datetime
import argparse
import json
import msvcrt
from pathlib import Path
import sys

from external_runtime_probe import ROOT, run_probe


def resolve_runtime_manifest(name):
    inputs = (ROOT / 'docs/inputs').resolve()
    candidate = (inputs / name).resolve()
    if not candidate.is_relative_to(inputs):
        raise ValueError('Runtime manifest must stay inside docs/inputs')
    if not candidate.is_file():
        raise ValueError(f'Runtime manifest is not available: {name}')
    return candidate


def play(aspect='16:9', runtime='slice114-native-base.json'):
    play_directory = ROOT / 'private/manual-play'
    if not play_directory.resolve().is_relative_to((ROOT / 'private').resolve()):
        raise ValueError('Manual play directory must remain inside private/')
    play_directory.mkdir(parents=True, exist_ok=True)
    # Windows releases this byte lock even when the launcher process crashes.
    with (play_directory / 'session.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise ValueError('Another manual game session is already running') from error
        try:
            run_directory = play_directory / datetime.now().strftime('session-%Y%m%d-%H%M%S-%f')
            card = play_directory / 'card.ps2'
            manifest = resolve_runtime_manifest(runtime)
            video = json.loads(manifest.read_text(encoding='utf-8'))
            seed_key = 'default_card_seed' if aspect == '16:9' else 'standard_card_seed'
            seed = None if card.exists() else ROOT / video[seed_key]['path']
            print('GT4 experimental: audio + XInput. Close window to exit.', flush=True)
            print('Keyboard: Enter starts, arrow keys steer, Z accelerates, A brakes; S returns in options.', flush=True)
            print(f'Display: {aspect}. F6 toggles 16:9/4:3; match aspect ratio in GT4 options.', flush=True)
            print(f'Runtime manifest: {manifest.name}', flush=True)
            print(f'Local save: {card}\nSession log: {run_directory}', flush=True)
            result = run_probe(ROOT, run_directory, 0, 43200, [], seed,
                               manifest,
                               window=True, interactive=True, play_card=card, aspect=aspect)
            if not result.get('manual_session_closed'):
                raise ValueError(f"Game stopped: {result.get('reason', 'see probe.log')}; log: {run_directory}")
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aspect', choices=('16:9', '4:3'), default='16:9')
    parser.add_argument('--runtime', default='slice114-native-base.json',
                        help='Runtime manifest name inside docs/inputs; the qualified '
                             'baseline stays the default')
    options = parser.parse_args()
    try:
        play(options.aspect, options.runtime)
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
