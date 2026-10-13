"""Open the qualified Windows pilot with a persistent private memory card."""
from datetime import datetime
import msvcrt
from pathlib import Path
import sys

from external_runtime_probe import ROOT, run_probe


def play():
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
            seed = None if card.exists() else ROOT / 'private/slice110-menu-pilot/name-control4/card.ps2'
            print('GT4 experimental: audio + XInput. Feche a janela para sair.', flush=True)
            print('Teclado: Enter inicia, setas dirigem, Z acelera, A freia, X volta.', flush=True)
            print(f'Save local: {card}\nRegistro: {run_directory}', flush=True)
            result = run_probe(ROOT, run_directory, 0, 43200, [], seed,
                               ROOT / 'docs/inputs/slice112-native-input-audio.json',
                               window=True, interactive=True, play_card=card)
            if not result.get('manual_session_closed'):
                raise ValueError(f"Game stopped: {result.get('reason', 'see probe.log')}; log: {run_directory}")
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


if __name__ == '__main__':
    try:
        play()
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f'FAIL: {error}', file=sys.stderr)
        raise SystemExit(1)
