"""Run the qualified external CPU pilot with pinned inputs and private outputs."""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

from gt4disc import fingerprint


ROOT = Path(__file__).resolve().parents[1]


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


def run_probe(workspace, run_directory, frame_count, timeout_seconds, presses, card_seed=None):
    workspace = workspace.resolve()
    run_directory = run_directory.resolve()
    private_root = workspace / 'private'
    if not run_directory.is_relative_to(private_root) or run_directory == private_root:
        raise ValueError('Probe output must be a new directory inside private/')
    if run_directory.exists():
        raise ValueError('Probe output already exists; preserve it and select a new directory')
    if frame_count <= 0 or timeout_seconds <= 0:
        raise ValueError('Frames and timeout must be positive')

    inputs = workspace / 'docs/inputs'
    reference = json.loads((inputs / 'external-recomp-references.json').read_text(encoding='utf-8'))
    native = json.loads((inputs / 'usa-v2.00-native.json').read_text(encoding='utf-8'))
    disc_manifest = json.loads((inputs / 'usa-v2.00.json').read_text(encoding='utf-8'))
    executable_pin = next(item for item in reference['artifacts'] if item['path'].endswith('/ps2run.exe'))
    executable = workspace / executable_pin['path']
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
        seed_pin = next((item for item in evidence['artifacts'] if item['path'] == relative_seed), None)
        if seed_pin is None:
            raise ValueError(f'Card seed has no pinned artifact: {card_seed}')
        verify_file(card_seed, {key: seed_pin[key] for key in ('size_bytes', 'sha256')})

    # Keep argv0 equal to the actual disc boot name without modifying the ELF.
    run_directory.mkdir(parents=True)
    guest = run_directory / 'SCUS_973.28'
    shutil.copyfile(source_elf, guest)
    verify_file(guest, {'size_bytes': native['native_elf_size'], 'sha256': native['native_elf_sha256']})
    if card_seed is not None:
        card = run_directory / 'card.ps2'
        shutil.copyfile(card_seed, card)
        verify_file(card, {key: seed_pin[key] for key in ('size_bytes', 'sha256')})
    frames = run_directory / 'frames'
    frames.mkdir()
    arguments = [str(executable), str(guest), '--ntsc', '--disc', str(disc),
                 '--frames', str(frame_count), '--memcard', str(run_directory / 'card.ps2'),
                 '--misses', str(run_directory / 'misses.txt'), '--frames-dir', str(frames),
                 '--frame-every', '100', '--trace-syscalls']
    for press in presses:
        arguments.extend(['--press', press])
    result = {'arguments': arguments, 'timeout_seconds': timeout_seconds,
              'disc_sha256': disc_manifest['disc']['sha256'],
              'runtime_sha256': executable_pin['sha256']}
    if seed_pin is not None:
        result['card_seed'] = seed_pin
    start = time.monotonic()
    log_path = run_directory / 'probe.log'
    with log_path.open('wb') as output:
        try:
            process = subprocess.run(arguments, cwd=run_directory, stdout=output,
                                     stderr=subprocess.STDOUT, timeout=timeout_seconds)
            exit_code, timed_out = process.returncode, False
        except subprocess.TimeoutExpired:
            exit_code, timed_out = None, True
    result.update(summarize_exit(log_path.read_text(encoding='utf-8', errors='replace'),
                                 frame_count, exit_code, timed_out))
    result['elapsed_seconds'] = time.monotonic() - start
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
    options = parser.parse_args()
    try:
        result = run_probe(ROOT, options.run_directory, options.frames, options.timeout,
                           options.press, options.card_seed)
    except (OSError, ValueError, KeyError, StopIteration) as error:
        parser.exit(1, f'FAIL: {error}\n')
    print(json.dumps(result, indent=2))
    return 0 if result['completed_requested_frames'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
