"""Synthetic profile-comparison controls; no game data or execution."""

import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts'))
import ee_entry_coverage as coverage


def controls():
    rows = {}
    for name in coverage.PROFILES:
        present = name != 'baseline'
        supplier_present = name in ('supplier_seed', 'both_seeds')
        rows[name] = {'callbacks': {
            '0x0044EA58': {'entry_id': 1 if present else 0, 'body_label': present},
            '0x0044EAC8': {'entry_id': 2 if supplier_present else 0, 'body_label': supplier_present}},
            'continuation_labels': {'0x0044EA90': present, '0x0044EAB8': present, '0x0044EB00': supplier_present},
            'literal_wrapper_call_sites': 12 if present else 10}
    return rows


def analysis_controls(rows):
    return {name: {'candidates': [
        {'address': address, 'code': row['body_label'], 'entry': bool(row['entry_id']), 'rejected': False}
        for address, row in profile['callbacks'].items()] + [
            {'address': '0x0044EB10', 'code': True, 'entry': True, 'rejected': False}]}
        for name, profile in rows.items()}


class CoverageControls(unittest.TestCase):
    def test_file_comparison_rejects_missing_units(self):
        baseline = [{'path': 'unit_0000.cpp', 'sha256': 'a', 'size_bytes': 1},
                    {'path': 'table.cpp', 'sha256': 'b', 'size_bytes': 1}]
        candidate = copy.deepcopy(baseline)
        candidate[0]['sha256'] = 'c'
        self.assertEqual(coverage.changed_files(baseline, candidate), ['unit_0000.cpp'])
        with self.assertRaises(ValueError):
            coverage.changed_files(baseline, candidate[:1])

    def test_recovery_requires_both_entries_and_return_continuations(self):
        analysis = analysis_controls(controls())
        coverage.qualify_profiles(analysis, controls())
        for mutation in ('entry', 'body', 'continuation', 'calls', 'baseline'):
            rows = controls()
            if mutation == 'entry':
                rows['supplier_seed']['callbacks']['0x0044EA58']['entry_id'] = 0
            elif mutation == 'body':
                rows['both_seeds']['callbacks']['0x0044EAC8']['body_label'] = False
            elif mutation == 'continuation':
                rows['supplier_seed']['continuation_labels']['0x0044EB00'] = False
            elif mutation == 'calls':
                rows['supplier_seed']['literal_wrapper_call_sites'] = 13
            else:
                rows['baseline']['callbacks']['0x0044EA58']['entry_id'] = 1
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                coverage.qualify_profiles(analysis, rows)

    def test_missing_profile_is_rejected(self):
        with self.assertRaises(ValueError):
            coverage.qualify_profiles({'baseline': {}}, controls())

    def test_callback_only_and_analyzer_disagreement_are_rejected(self):
        analysis = analysis_controls(controls())
        for mutation in ('empty', 'callback_body', 'supplier_body', 'return', 'calls'):
            rows = controls()
            if mutation == 'empty':
                rows['callback_seed']['callbacks'] = {}
            elif mutation == 'callback_body':
                rows['callback_seed']['callbacks']['0x0044EA58']['body_label'] = False
            elif mutation == 'supplier_body':
                rows['callback_seed']['callbacks']['0x0044EAC8']['body_label'] = True
            elif mutation == 'return':
                rows['callback_seed']['continuation_labels']['0x0044EB00'] = True
            else:
                rows['callback_seed']['literal_wrapper_call_sites'] = 10
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                coverage.qualify_profiles(analysis, rows)
        for mutation in ('empty', 'code', 'entry', 'rejected', 'dispatcher', 'duplicate'):
            observed = copy.deepcopy(analysis)
            candidates = observed['supplier_seed']['candidates']
            if mutation == 'empty':
                candidates.clear()
            elif mutation == 'dispatcher':
                candidates[2]['entry'] = False
            elif mutation == 'duplicate':
                candidates[1] = copy.deepcopy(candidates[0])
            else:
                candidates[0][mutation] = mutation == 'rejected'
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                coverage.qualify_profiles(observed, controls())


if __name__ == '__main__':
    unittest.main()
