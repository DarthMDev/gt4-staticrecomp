"""Open the qualified Windows pilot with a persistent private memory card."""
from datetime import datetime
import argparse
import json
import msvcrt
from pathlib import Path
import sys

from external_runtime_probe import ROOT, run_probe, verify_manual_capture_profile, verify_performance_profile


def play(aspect='16:9', assets_root=None, runtime_manifest=None, capture_shapes=False, performance=False):
    workspace = Path(assets_root or ROOT).resolve()
    if performance:
        manifest = Path(runtime_manifest).resolve()
        verify_performance_profile(manifest, json.loads(manifest.read_text(encoding='utf-8')), ROOT)
    play_directory = workspace / 'private/manual-play'
    if not play_directory.resolve().is_relative_to((workspace / 'private').resolve()):
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
            manifest = Path(runtime_manifest or ROOT / 'docs/inputs/slice114-native-base.json').resolve()
            if not manifest.is_relative_to(ROOT / 'docs/inputs'):
                raise ValueError('Manual runtime manifest must stay inside repository docs/inputs')
            video = json.loads(manifest.read_text(encoding='utf-8'))
            if capture_shapes:
                verify_manual_capture_profile(manifest, video, ROOT)
            seed_key = 'default_card_seed' if aspect == '16:9' else 'standard_card_seed'
            seed = None if card.exists() else workspace / video[seed_key]['path']
            print('GT4 experimental: audio + XInput. Close window to exit.', flush=True)
            print('Keyboard: Enter starts, arrow keys steer, Z accelerates, A brakes; S returns in options.', flush=True)
            print(f'Display: {aspect}. F6 toggles 16:9/4:3; match aspect ratio in GT4 options.', flush=True)
            print(f'Local save: {card}\nSession log: {run_directory}', flush=True)
            if capture_shapes:
                print('Capture starts OFF. Select the scene yourself, then press F8 to start/stop.', flush=True)
                print(f'One capture per launch, up to 256 records: {run_directory / "shape-capture.json"}', flush=True)
            if performance:
                print(f"Measurement mode: {video.get('measurement_mode', 'detailed')}", flush=True)
                print('Performance test: F7 starts, F8 reports, F9 toggles diagnostics. Owner controls gameplay.', flush=True)
            result = run_probe(workspace, run_directory, 0, 43200, [], seed,
                               manifest,
                               window=True, interactive=True, play_card=card, aspect=aspect,
                               runtime_workspace=ROOT, capture_shapes=capture_shapes, performance=performance)
            if not result.get('manual_session_closed'):
                raise ValueError(f"Game stopped: {result.get('reason', 'see probe.log')}; log: {run_directory}")
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--aspect', choices=('16:9', '4:3'), default='16:9')
    options = parser.parse_args()
    try:
        play(options.aspect)
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
