"""Literal controls for strict probe insertion and generated profile membership."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import ee_observation as observation
from ee_producer import generated_inventory


class ObservationGenerationTests(unittest.TestCase):
    def setUp(self):
        self.original = b'L_00455A5C:\n    toy_call();\nL_00455A64:\n    toy_counter();\n'
        self.expected = (b'L_00455A5C:\n    probe(c, 0x00455A5Cu);\n    toy_call();\n'
                         b'L_00455A64:\n    probe(c, 0x00455A64u);\n    toy_counter();\n')

    def test_exact_probe_file_and_two_insertions(self):
        observation.validate_probes(b'0x00455A5C\n0x00455A64\n')
        observation.qualify_unit(self.original, self.expected)

    def test_missing_malformed_duplicate_extra_addresses(self):
        for value in (b'', b'0x00455A5C\n', b'00455A5Cjunk\n00455A64\n',
                      b'0x00455A5C\n0x00455A5C\n', b'0x00455A5C\n0x00455A64\n0x0049D308\n',
                      b'\xef\xbb\xbf0x00455A5C\n0x00455A64\n', '0x00455A5C\n0x00455A64\n'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                observation.validate_probes(value)

    def test_missing_duplicate_or_already_instrumented_labels(self):
        for value in (self.original.replace(b'L_00455A64:', b'L_00455A68:'),
                      self.original + b'L_00455A5C:\n', self.expected):
            with self.subTest(value=value), self.assertRaises(ValueError):
                observation.qualify_unit(value, self.expected)

    def test_wrong_placement_and_guest_mutation_rejected(self):
        for value in (self.expected.replace(b'    toy_call();', b'    changed_call();'),
                      self.expected.replace(b'    probe(c, 0x00455A5Cu);\n    toy_call();',
                                            b'    toy_call();\n    probe(c, 0x00455A5Cu);'),
                      self.expected.replace(b'0x00455A64u', b'0x00455A68u'),
                      self.expected + b'    probe(c, 0x0049D308u);\n'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                observation.qualify_unit(self.original, value)

    def test_source_types_are_explicit(self):
        for original, candidate in ((self.original.decode(), self.expected),
                                    (self.original, bytearray(self.expected))):
            with self.assertRaises(ValueError):
                observation.qualify_unit(original, candidate)

    def make_profile(self, root):
        original, candidate = root / 'original', root / 'candidate'
        original.mkdir()
        candidate.mkdir()
        (original / 'unit_00D5.cpp').write_bytes(self.original)
        (candidate / 'unit_00D5.cpp').write_bytes(self.expected)
        for name in ('table.cpp', 'units.hpp', 'unit_0000.cpp'):
            (original / name).write_bytes(b'unchanged\n')
            (candidate / name).write_bytes(b'unchanged\n')
        return original, candidate, generated_inventory(original)

    def test_full_profile_membership_and_exact_other_files(self):
        with tempfile.TemporaryDirectory() as folder:
            original, candidate, records = self.make_profile(Path(folder))
            result = observation.qualify_profile(original, candidate, records)
            self.assertEqual(len(result), 4)
            (candidate / 'unit_0000.cpp').write_bytes(b'different\n')
            with self.assertRaisesRegex(ValueError, 'outside the selected'):
                observation.qualify_profile(original, candidate, records)

    def test_missing_extra_and_subdirectory_rejected(self):
        for mutation in ('missing', 'extra', 'directory'):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as folder:
                original, candidate, records = self.make_profile(Path(folder))
                if mutation == 'missing':
                    (candidate / 'table.cpp').unlink()
                elif mutation == 'extra':
                    (candidate / 'extra.cpp').write_bytes(b'')
                else:
                    (candidate / 'table.cpp').unlink()
                    (candidate / 'table.cpp').mkdir()
                with self.assertRaises(ValueError):
                    observation.qualify_profile(original, candidate, records)

    def test_extent_bound_precedes_candidate_read(self):
        with tempfile.TemporaryDirectory() as folder:
            original, candidate, records = self.make_profile(Path(folder))
            with (candidate / 'unit_00D5.cpp').open('wb') as stream:
                stream.truncate(32 * 1024 * 1024)
            with self.assertRaisesRegex(ValueError, 'extent'):
                observation.qualify_profile(original, candidate, records)

    def test_baseline_missing_unit_or_duplicate_filename_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            original, candidate, records = self.make_profile(Path(folder))
            for invalid in ([row for row in records if row['path'] != 'unit_00D5.cpp'],
                            records + [records[0]]):
                with self.assertRaisesRegex(ValueError, 'Baseline profile'):
                    observation.qualify_profile(original, candidate, invalid)


if __name__ == '__main__':
    unittest.main()
