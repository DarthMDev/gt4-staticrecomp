"""Control preparation and reject forged observation library receipts."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import ee_observation_library as library
from ee_producer import generated_inventory, identity


class ObservationLibraryTests(unittest.TestCase):
    def test_exact_one_object_delta_also_needs_independent_d5_identity(self):
        original = [{'name': 'table.cpp.obj', 'sha256_without_timestamp': 'same'},
                    {'name': 'unit_00D5.cpp.obj', 'sha256_without_timestamp': 'baseline'}]
        observed = [original[0], {'name': 'unit_00D5.cpp.obj', 'sha256_without_timestamp': 'qualified'}]
        self.assertEqual(library.qualify_objects(original, observed, 'qualified'), ['unit_00D5.cpp.obj'])
        with self.assertRaisesRegex(ValueError, 'independently compiled'):
            library.qualify_objects(original, observed, 'wrong')
        with self.assertRaises(ValueError):
            library.qualify_objects(original, original, 'baseline')

    def test_only_generated_directory_may_differ_in_compiler_recipe(self):
        with tempfile.TemporaryDirectory() as folder:
            first, second = Path(folder) / 'first', Path(folder) / 'second'
            first.write_text('compiler=clang-cl\nflags=O2\ngenerated=baseline\n')
            second.write_text('compiler=clang-cl\nflags=O2\ngenerated=candidate\n')
            library.qualify_recipe(first, second)
            for wrong in ('compiler=other\nflags=O2\ngenerated=candidate\n',
                          'compiler=clang-cl\nflags=O1\ngenerated=candidate\n',
                          'compiler=clang-cl\nflags=O2\ngenerated=candidate\nextra=unreviewed\n'):
                second.write_text(wrong)
                with self.assertRaisesRegex(ValueError, 'recipe changed'):
                    library.qualify_recipe(first, second)

    def test_missing_or_duplicate_generated_path_is_not_normalized_away(self):
        with tempfile.TemporaryDirectory() as folder:
            first, second = Path(folder) / 'first', Path(folder) / 'second'
            first.write_text('compiler=clang-cl\ngenerated=baseline\n')
            for wrong in ('compiler=clang-cl\n', 'compiler=clang-cl\ngenerated=a\ngenerated=b\n'):
                second.write_text(wrong)
                with self.assertRaisesRegex(ValueError, 'one generated-source'):
                    library.qualify_recipe(first, second)

    def workspace(self, root):
        baseline, profile = root / 'baseline', root / 'profile'
        (baseline / 'game-build').mkdir(parents=True)
        (baseline / 'game-build/ps2game.lib').write_bytes(b'pinned baseline')
        (profile / 'generated').mkdir(parents=True)
        (profile / 'generated/unit_00D5.cpp').write_bytes(b'literal reviewed source\n')
        return baseline, profile, {'assets_root': str(root / 'assets')}, generated_inventory(profile / 'generated')

    def test_prepared_copy_is_exact_and_inputs_unchanged(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            baseline, profile, metadata, records = self.workspace(root)
            output = root / 'new-library'
            with patch.object(library, 'reviewed_sources', return_value=(baseline, profile, metadata, records)), \
                    patch.object(library, 'validate_output', return_value=output):
                result = library.prepare_library(baseline, profile, output)
            self.assertEqual(generated_inventory(output / 'generated'), records)
            self.assertEqual(generated_inventory(profile / 'generated'), records)
            self.assertEqual(result['baseline_library'], identity(baseline / 'game-build/ps2game.lib'))
            self.assertTrue((output / 'library-preparation.json').is_file())

    def test_source_review_failure_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with patch.object(library, 'reviewed_sources', side_effect=ValueError('source mismatch')):
                with self.assertRaisesRegex(ValueError, 'source mismatch'):
                    library.prepare_library(root / 'baseline', root / 'profile', root / 'output')
            self.assertFalse((root / 'output').exists())

    def test_forged_baseline_pin_cannot_reach_compilation_receipt(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            baseline, profile, metadata, records = self.workspace(root)
            output = root / 'candidate'
            output.mkdir()
            (output / 'library-preparation.json').write_text(json.dumps({
                'baseline': str(baseline), 'profile': str(profile), 'baseline_library': {'sha256': 'forged'},
                'generated': records}))
            with patch.object(library, 'producer_path', return_value=output), \
                    patch.object(library, 'reviewed_sources', return_value=(baseline, profile, metadata, records)), \
                    patch.object(library, 'verify_toolchain') as compiler:
                with self.assertRaisesRegex(ValueError, 'cannot repin'):
                    library.record_library(output)
                compiler.assert_not_called()
            self.assertFalse((output / 'observation-library.json').exists())

    def test_changed_prepared_source_cannot_reach_library_reader(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            baseline, profile, metadata, records = self.workspace(root)
            output = root / 'candidate'
            (output / 'generated').mkdir(parents=True)
            (output / 'generated/unit_00D5.cpp').write_bytes(b'changed source\n')
            (output / 'library-preparation.json').write_text(json.dumps({
                'baseline': str(baseline), 'profile': str(profile),
                'baseline_library': identity(baseline / 'game-build/ps2game.lib'), 'generated': records}))
            with patch.object(library, 'producer_path', return_value=output), \
                    patch.object(library, 'reviewed_sources', return_value=(baseline, profile, metadata, records)), \
                    patch.object(library, 'verify_toolchain') as compiler:
                with self.assertRaisesRegex(ValueError, 'source changed'):
                    library.record_library(output)
                compiler.assert_not_called()
            self.assertFalse((output / 'observation-library.json').exists())

    def test_oversized_archive_rejected_before_loading_object_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            baseline, profile, metadata, records = self.workspace(root)
            output = root / 'candidate'
            (output / 'generated').mkdir(parents=True)
            (output / 'game-build').mkdir()
            (output / 'generated/unit_00D5.cpp').write_bytes(b'literal reviewed source\n')
            with (output / 'game-build/ps2game.lib').open('wb') as stream:
                stream.truncate(128 * 1024 * 1024 + 1)
            (output / 'library-preparation.json').write_text(json.dumps({
                'baseline': str(baseline), 'profile': str(profile),
                'baseline_library': identity(baseline / 'game-build/ps2game.lib'), 'generated': records}))
            with patch.object(library, 'producer_path', return_value=output), \
                    patch.object(library, 'reviewed_sources', return_value=(baseline, profile, metadata, records)), \
                    patch.object(library, 'verify_toolchain'), patch.object(library, 'qualify_recipe'), \
                    patch.object(library, 'object_inventory') as reader:
                with self.assertRaisesRegex(ValueError, 'read limit'):
                    library.record_library(output)
                reader.assert_not_called()


if __name__ == '__main__':
    unittest.main()
