"""--play-card (TASK-017 license checks): a separate private test card; the owner card is never used or touched.

No game is launched: run_probe and the game process are mocked.
"""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import external_runtime_probe as probe  # noqa: E402
import play_external_runtime as launcher  # noqa: E402
import play_performance as performance  # noqa: E402
import test_external_runtime_probe as probe_tests  # noqa: E402

CARD_BYTES = 8650752


def workspace_with_cards(root):
    owner = root / 'private/manual-play/card.ps2'
    owner.parent.mkdir(parents=True)
    owner.write_bytes(b'o' * CARD_BYTES)
    test_card = root / 'private/test-cards/gt4-100pct.ps2'
    test_card.parent.mkdir(parents=True)
    test_card.write_bytes(b't' * CARD_BYTES)
    return owner, test_card


class SeparatePlayCardValidationTests(unittest.TestCase):
    def test_accepts_an_existing_private_card_relative_or_absolute(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            _, test_card = workspace_with_cards(root)
            self.assertEqual(probe.separate_play_card(root, 'private/test-cards/gt4-100pct.ps2'), test_card)
            self.assertEqual(probe.separate_play_card(root, test_card), test_card)

    def test_refuses_the_owner_card(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            owner, _ = workspace_with_cards(root)
            for path in (owner, 'private/manual-play/card.ps2', 'private/test-cards/../manual-play/card.ps2'):
                with self.subTest(path=path), self.assertRaisesRegex(ValueError, "owner's card"):
                    probe.separate_play_card(root, path)

    def test_refuses_paths_outside_private(self):
        with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as other:
            root = Path(folder).resolve()
            workspace_with_cards(root)
            outside = Path(other) / 'card.ps2'
            outside.write_bytes(b'x' * CARD_BYTES)
            (root / 'docs').mkdir()
            inside_docs = root / 'docs/card.ps2'
            inside_docs.write_bytes(b'x' * CARD_BYTES)
            for path in (outside, inside_docs, 'private/../docs/card.ps2', 'private'):
                with self.subTest(path=path), self.assertRaisesRegex(ValueError, 'inside private/'):
                    probe.separate_play_card(root, path)

    def test_refuses_a_missing_card_and_never_creates_it(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            workspace_with_cards(root)
            missing = root / 'private/test-cards/missing.ps2'
            with self.assertRaisesRegex(ValueError, 'existing card'):
                probe.separate_play_card(root, missing)
            self.assertFalse(missing.exists())


class PlayCardLauncherTests(unittest.TestCase):
    def manifest(self, root):
        inputs = root / 'docs/inputs'
        inputs.mkdir(parents=True)
        manifest = inputs / 'slice114-native-base.json'
        manifest.write_text(json.dumps({'default_card_seed': {'path': 'private/seed.ps2'}}))
        return manifest

    def test_play_uses_the_separate_card_and_leaves_the_owner_card(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            owner, test_card = workspace_with_cards(root)
            manifest = self.manifest(root)
            seen = {}

            def execute(*arguments, **options):
                seen.update(options, seed=arguments[5])
                return {'manual_session_closed': True}
            with patch.object(launcher, 'ROOT', root), patch.object(launcher, 'run_probe', side_effect=execute):
                launcher.play(root, manifest, play_card='private/test-cards/gt4-100pct.ps2')
            self.assertEqual(seen['play_card'], test_card)
            self.assertTrue(seen['separate_card'])
            self.assertIsNone(seen['seed'])
            self.assertEqual(owner.read_bytes(), b'o' * CARD_BYTES)

    def test_play_without_the_flag_keeps_the_owner_card(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            owner, _ = workspace_with_cards(root)
            manifest = self.manifest(root)
            seen = {}
            with patch.object(launcher, 'ROOT', root), \
                    patch.object(launcher, 'run_probe', side_effect=lambda *a, **o: seen.update(o) or
                                 {'manual_session_closed': True}):
                launcher.play(root, manifest)
            self.assertEqual(seen['play_card'], owner)
            self.assertFalse(seen['separate_card'])

    def test_play_refuses_the_owner_card_before_any_session(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            owner, _ = workspace_with_cards(root)
            manifest = self.manifest(root)
            with patch.object(launcher, 'ROOT', root), patch.object(launcher, 'run_probe') as execute:
                with self.assertRaisesRegex(ValueError, "owner's card"):
                    launcher.play(root, manifest, play_card=owner)
            execute.assert_not_called()
            self.assertFalse(any((root / 'private/manual-play').glob('session-*')))

    def test_play_performance_forwards_the_flag(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder).resolve()
            with patch.object(performance, 'play') as play, patch.object(sys, 'argv', [
                    'play', '--assets-root', str(root), '--mode', 'pcsx2', '--play-card',
                    'private/test-cards/gt4-100pct.ps2']):
                self.assertEqual(performance.main(), 0)
            self.assertEqual(play.call_args.kwargs['play_card'], Path('private/test-cards/gt4-100pct.ps2'))
            with patch.object(performance, 'play') as play, patch.object(sys, 'argv', [
                    'play', '--assets-root', str(root), '--mode', 'pcsx2']):
                self.assertEqual(performance.main(), 0)
            self.assertNotIn('play_card', play.call_args.kwargs)


class PlayCardProbeResultTests(unittest.TestCase):
    """run_probe with a separate card: backed up like the owner card and recorded in result.json."""

    make_workspace = probe_tests.ProbeTests.make_workspace

    def test_separate_card_is_backed_up_and_recorded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            (root / 'docs/inputs/slice110-external-menu-pilot.json').write_text(json.dumps({'artifacts': []}))
            owner, test_card = workspace_with_cards(root)

            def execute(arguments, **options):
                self.assertEqual(arguments[arguments.index('--memcard') + 1], str(test_card.resolve()))
                options['stdout'].write(b'[ee] stopped after 77 frames, 99 cycles: window closed\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.object(probe.subprocess, 'run', side_effect=execute):
                probe.run_probe(root, root / 'private/play', 0, 1, [], window=True, interactive=True,
                                play_card=test_card, separate_card=True)
            result = json.loads((root / 'private/play/result.json').read_text(encoding='utf-8'))
            self.assertEqual(result['play_card_option'], str(test_card.resolve()))
            self.assertEqual(result['play_card_path'], str(test_card.resolve()))
            self.assertEqual((root / 'private/play/card-before.ps2').read_bytes(), b't' * CARD_BYTES)
            self.assertEqual(owner.read_bytes(), b'o' * CARD_BYTES)
            with self.assertRaisesRegex(ValueError, "owner's card"):
                probe.run_probe(root, root / 'private/owner', 0, 1, [], window=True, interactive=True,
                                play_card=owner, separate_card=True)
            self.assertFalse((root / 'private/owner').exists())

    def test_owner_card_session_has_no_option_field(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            self.make_workspace(root)
            (root / 'docs/inputs/slice110-external-menu-pilot.json').write_text(json.dumps({'artifacts': []}))
            owner, _ = workspace_with_cards(root)

            def execute(arguments, **options):
                options['stdout'].write(b'[ee] stopped after 77 frames, 99 cycles: window closed\n')
                return subprocess.CompletedProcess(arguments, 0)
            with patch.object(probe.subprocess, 'run', side_effect=execute):
                result = probe.run_probe(root, root / 'private/play', 0, 1, [], window=True, interactive=True,
                                         play_card=owner)
            self.assertNotIn('play_card_option', result)


if __name__ == '__main__':
    unittest.main()
