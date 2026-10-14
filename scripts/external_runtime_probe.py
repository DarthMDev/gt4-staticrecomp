"""Run a qualified external pilot with pinned inputs and private outputs."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time

from gt4disc import fingerprint


ROOT = Path(__file__).resolve().parents[1]
CAPTURE_MANIFEST = 'slice136-keyboard-input.json'
CAPTURE_RUNTIME = 'private/slice136-keyboard-input/build/gt4vulkan.exe'


def verify_manual_capture_profile(manifest, evidence, runtime_workspace):
    schema = evidence.get('capture_schema_version')
    if (manifest != (runtime_workspace / 'docs/inputs' / CAPTURE_MANIFEST).resolve() or evidence.get('profile') != 'observation' or
            evidence.get('capture_activation_implemented') is not True or type(schema) is not int or schema != 1 or
            evidence.get('runtime', {}).get('path') != CAPTURE_RUNTIME):
        raise ValueError('Shape capture requires the qualified manual observation runtime')


def verify_file(path, expected):
    with path.open('rb') as source:
        actual = fingerprint(source)
    if actual != expected:
        raise ValueError(f'Input differs from its pinned size/hash: {path}')


def summarize_exit(log_text, frame_count, exit_code, timed_out):
    stops = re.findall(r'\[ee\] stopped after (\d+) frames, (\d+) cycles: (.+)', log_text)
    summary = {'exit_code': exit_code, 'timed_out': timed_out,
               'completed_requested_frames': False}
    if stops:
        frames, budget, reason = stops[-1]
        summary.update(reported_frames=int(frames), reported_budget=int(budget), reason=reason.strip())
        summary['completed_requested_frames'] = (
            not timed_out and exit_code == 0 and int(frames) == frame_count
            and reason.strip() == 'frame limit reached')
    return summary


def run_probe(workspace, run_directory, frame_count, timeout_seconds, presses, card_seed=None,
              runtime_manifest=None, window=False, capture_audio=False, interactive=False,
              play_card=None, aspect='16:9', runtime_workspace=None, capture_shapes=False):
    workspace = workspace.resolve()
    runtime_workspace = Path(runtime_workspace or workspace).resolve()
    runtime_private = runtime_workspace / 'private'
    if capture_shapes and run_directory.absolute() != run_directory.resolve():
        raise ValueError('Shape capture session must use an ordinary canonical private path')
    run_directory = run_directory.resolve()
    private_root = workspace / 'private'
    if aspect not in ('16:9', '4:3'):
        raise ValueError('Presentation aspect must be 16:9 or 4:3')
    if not run_directory.is_relative_to(private_root) or run_directory == private_root:
        raise ValueError('Probe output must be a new directory inside private/')
    if run_directory.exists():
        raise ValueError('Probe output already exists; preserve it and select a new directory')
    if (frame_count <= 0 and not (interactive and frame_count == 0)) or timeout_seconds <= 0:
        raise ValueError('Frames and timeout must be positive')
    if interactive and (not window or presses or frame_count != 0):
        raise ValueError('Interactive play requires a window, zero frame limit and no scripted presses')
    if capture_shapes and not interactive:
        raise ValueError('Shape capture requires owner-controlled interactive play')
    if play_card is not None:
        play_card = play_card.resolve()
        if not interactive or not play_card.is_relative_to(private_root):
            raise ValueError('Persistent play card requires interactive mode and a private path')
        if play_card.exists() and play_card.stat().st_size != 8650752:
            raise ValueError('Persistent card has an unexpected size; preserve it for inspection')
        if not play_card.exists() and card_seed is None:
            raise ValueError('First manual play needs a pinned seed card')

    inputs = workspace / 'docs/inputs'
    native = json.loads((inputs / 'usa-v2.00-native.json').read_text(encoding='utf-8'))
    disc_manifest = json.loads((inputs / 'usa-v2.00.json').read_text(encoding='utf-8'))
    if runtime_manifest is None:
        runtime_manifest = runtime_workspace / 'docs/inputs/slice114-native-base.json'
    runtime_manifest = runtime_manifest.resolve()
    if not runtime_manifest.is_relative_to(runtime_workspace / 'docs/inputs'):
        raise ValueError('Runtime manifest must be inside docs/inputs')
    runtime_evidence = json.loads(runtime_manifest.read_text(encoding='utf-8'))
    if capture_shapes:
        verify_manual_capture_profile(runtime_manifest, runtime_evidence, runtime_workspace)
    executable_pin = runtime_evidence['runtime']
    for dependency in runtime_evidence.get('runtime_dependencies', []):
        dependency_path = (runtime_workspace / dependency['path']).resolve()
        if not dependency_path.is_relative_to(runtime_private):
            raise ValueError('Runtime dependency must be inside private/')
        verify_file(dependency_path, {key: dependency[key] for key in ('size_bytes', 'sha256')})
    executable = runtime_workspace / executable_pin['path']
    if not executable.resolve().is_relative_to(runtime_private):
        raise ValueError('Runtime executable must be inside private/')
    verify_file(executable, {key: executable_pin[key] for key in ('size_bytes', 'sha256')})
    core = workspace / 'private/fingerprint-check/CORE.GT4'
    verify_file(core, disc_manifest['files']['/CORE.GT4;1'])
    source_elf = workspace / 'private/reconstructed/SCUS_973.28.elf'
    verify_file(source_elf, {'size_bytes': native['native_elf_size'], 'sha256': native['native_elf_sha256']})
    disc = workspace / 'Gran Turismo 4 (USA) (v2.00).iso'
    verify_file(disc, disc_manifest['disc'])

    seed_pin = None
    if card_seed is not None:
        card_seed = card_seed.resolve()
        if not card_seed.is_relative_to(private_root):
            raise ValueError('Card seed must be inside private/')
        evidence = json.loads((inputs / 'slice110-external-menu-pilot.json').read_text(encoding='utf-8'))
        relative_seed = card_seed.relative_to(workspace).as_posix()
        qualified_seeds = evidence['artifacts'] + runtime_evidence.get('card_seeds', [])
        seed_pin = next((item for item in qualified_seeds if item['path'] == relative_seed), None)
        if seed_pin is None:
            raise ValueError(f'Card seed has no pinned artifact: {card_seed}')
        verify_file(card_seed, {key: seed_pin[key] for key in ('size_bytes', 'sha256')})
    if play_card is not None:
        evidence = json.loads((inputs / 'slice110-external-menu-pilot.json').read_text(encoding='utf-8'))
        protected = {item['path'] for item in evidence['artifacts'] + runtime_evidence.get('card_seeds', [])}
        if play_card.relative_to(workspace).as_posix() in protected:
            raise ValueError('Persistent play must never overwrite a pinned evidence artifact')

    # Keep argv0 equal to the actual disc boot name without modifying the ELF.
    run_directory.mkdir(parents=True)
    guest = run_directory / 'SCUS_973.28'
    shutil.copyfile(source_elf, guest)
    verify_file(guest, {'size_bytes': native['native_elf_size'], 'sha256': native['native_elf_sha256']})
    if card_seed is not None:
        card = run_directory / 'card.ps2'
        shutil.copyfile(card_seed, card)
        verify_file(card, {key: seed_pin[key] for key in ('size_bytes', 'sha256')})
    if play_card is not None:
        if not play_card.exists():
            play_card.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(run_directory / 'card.ps2', play_card)
            verify_file(play_card, {key: seed_pin[key] for key in ('size_bytes', 'sha256')})
        shutil.copyfile(play_card, run_directory / 'card-before.ps2')
    frames = run_directory / 'frames'
    if not interactive:
        frames.mkdir()
    arguments = [str(executable), str(guest), '--ntsc', '--disc', str(disc),
                 '--frames', str(frame_count), '--memcard', str(play_card or run_directory / 'card.ps2'),
                 '--misses', str(run_directory / 'misses.txt')]
    if not interactive:
        arguments.extend(['--frames-dir', str(frames), '--frame-every', '100', '--trace-syscalls'])
    for press in presses:
        arguments.extend(['--press', press])
    if window:
        arguments.append('--window')
    result = {'arguments': arguments, 'timeout_seconds': timeout_seconds,
              'presentation_aspect': aspect,
              'disc_sha256': disc_manifest['disc']['sha256'],
              'runtime_sha256': executable_pin['sha256']}
    if seed_pin is not None:
        result['card_seed'] = seed_pin
    if play_card is not None:
        with (run_directory / 'card-before.ps2').open('rb') as source:
            result['play_card_before'] = fingerprint(source)
        result['play_card_path'] = str(play_card)
    start = time.monotonic()
    log_path = run_directory / 'probe.log'
    environment = os.environ.copy()
    environment['GT4_ASPECT'] = aspect
    environment.pop('PS2_DUMP_AUDIO', None)
    environment.pop('GT4_OBSERVATION_OUTPUT', None)
    if capture_shapes:
        environment['GT4_OBSERVATION_OUTPUT'] = str(run_directory / 'shape-capture.json')
        result['shape_capture_path'] = environment['GT4_OBSERVATION_OUTPUT']
    if capture_audio:
        environment['PS2_DUMP_AUDIO'] = str(run_directory / 'audio.wav')
    with log_path.open('wb') as output:
        try:
            process = subprocess.run(arguments, cwd=run_directory, stdout=output,
                                     stderr=subprocess.STDOUT, timeout=timeout_seconds, env=environment)
            exit_code, timed_out = process.returncode, False
        except subprocess.TimeoutExpired:
            exit_code, timed_out = None, True
    result.update(summarize_exit(log_path.read_text(encoding='utf-8', errors='replace'),
                                 frame_count, exit_code, timed_out))
    result['elapsed_seconds'] = time.monotonic() - start
    if interactive:
        result['manual_session_closed'] = (not timed_out and exit_code == 0
                                           and result.get('reason') == 'window closed')
    if play_card is not None:
        with play_card.open('rb') as source:
            result['play_card_after'] = fingerprint(source)
    if capture_shapes:
        capture = run_directory / 'shape-capture.json'
        result['shape_capture_created'] = capture.exists()
        if capture.exists():
            if capture.is_symlink() or capture.is_junction() or capture.stat().st_size > 256 * 1024:
                raise ValueError('Shape capture output exceeds the ordinary bounded-file contract')
            with capture.open('rb') as source:
                result['shape_capture_file'] = fingerprint(source)
    result['frames'] = sorted(path.name for path in frames.glob('*.ppm'))
    (run_directory / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_directory', type=Path)
    parser.add_argument('--frames', type=int, default=2000)
    parser.add_argument('--timeout', type=int, default=110)
    parser.add_argument('--press', action='append', default=[])
    parser.add_argument('--card-seed', type=Path, help='Copy a private card pinned in slice110 evidence')
    parser.add_argument('--runtime-manifest', type=Path, help='Select a separately pinned experimental runtime')
    parser.add_argument('--window', action='store_true', help='Present real guest output in a native window')
    parser.add_argument('--capture-audio', action='store_true', help='Record guest PCM in private audio.wav')
    parser.add_argument('--interactive', action='store_true', help='Manual window play without scripted input or captures')
    parser.add_argument('--aspect', choices=('16:9', '4:3'), default='16:9', help='Presentation ratio; match the game Screen Ratio option')
    options = parser.parse_args()
    try:
        result = run_probe(ROOT, options.run_directory, options.frames, options.timeout,
                           options.press, options.card_seed, options.runtime_manifest, options.window,
                           options.capture_audio, options.interactive, aspect=options.aspect)
    except (OSError, ValueError, KeyError, StopIteration) as error:
        parser.exit(1, f'FAIL: {error}\n')
    print(json.dumps(result, indent=2))
    return 0 if result['completed_requested_frames'] or result.get('manual_session_closed') else 1


if __name__ == '__main__':
    raise SystemExit(main())
