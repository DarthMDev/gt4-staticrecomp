"""TASK-011 step 6 (docs/MTVU-PLAN.md section 12): --cpu-cores selection, validation and refusal.

Pure checks on synthetic topologies (always run). The Windows calls are not exercised here: GetLogicalProcessorInformation
and SetProcessAffinityMask run only on the owner machine, through the launcher.
"""
import sys
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import cpu_affinity as affinity
import play_performance as performance

# Eight physical cores with SMT, numbered the Windows way: core i holds logical CPUs 2i and 2i+1.
SMT_8X2 = [[2 * index, 2 * index + 1] for index in range(8)]
# Four cores without SMT.
PLAIN_4 = [[index] for index in range(4)]


class SelectionTests(unittest.TestCase):
    def test_two_cores_use_distinct_physical_cores(self):
        cpus = affinity.select_logical_cpus(SMT_8X2, 2)
        self.assertEqual(cpus, [0, 2])
        self.assertEqual(affinity.affinity_mask(cpus), 0b101)

    def test_one_core_takes_the_first_thread_of_core_zero(self):
        self.assertEqual(affinity.select_logical_cpus(SMT_8X2, 1), [0])

    def test_all_physical_cores_before_any_sibling(self):
        cpus = affinity.select_logical_cpus(SMT_8X2, 8)
        self.assertEqual(cpus, [0, 2, 4, 6, 8, 10, 12, 14])
        self.assertEqual(len({cpu // 2 for cpu in cpus}), 8)

    def test_siblings_only_after_every_core_is_used(self):
        cpus = affinity.select_logical_cpus(SMT_8X2, 10)
        # Eight first threads, then the siblings of cores 0 and 1 (logical 1 and 3).
        self.assertEqual(cpus, [0, 1, 2, 3, 4, 6, 8, 10, 12, 14])

    def test_all_logical_cpus(self):
        self.assertEqual(affinity.select_logical_cpus(SMT_8X2, 16), list(range(16)))

    def test_plain_topology(self):
        self.assertEqual(affinity.select_logical_cpus(PLAIN_4, 3), [0, 1, 2])

    def test_request_outside_topology_is_refused(self):
        with self.assertRaises(ValueError):
            affinity.select_logical_cpus(SMT_8X2, 17)
        with self.assertRaises(ValueError):
            affinity.select_logical_cpus(SMT_8X2, 0)


class MaskTests(unittest.TestCase):
    def test_mask_bits(self):
        self.assertEqual(affinity.affinity_mask([0]), 1)
        self.assertEqual(affinity.affinity_mask([0, 2]), 0b101)
        self.assertEqual(affinity.affinity_mask(list(range(16))), 0xFFFF)

    def test_empty_and_out_of_group_are_refused(self):
        with self.assertRaises(ValueError):
            affinity.affinity_mask([])
        with self.assertRaises(ValueError):
            affinity.affinity_mask([64])
        with self.assertRaises(ValueError):
            affinity.affinity_mask([-1])


class ValidationTests(unittest.TestCase):
    def test_range_is_one_to_logical_count(self):
        self.assertEqual(affinity.validate_cpu_cores(1, 16), 1)
        self.assertEqual(affinity.validate_cpu_cores(16, 16), 16)
        for value in (0, -1, 17):
            with self.assertRaises(ValueError):
                affinity.validate_cpu_cores(value, 16)

    def test_non_integers_are_refused(self):
        for value in (True, 2.0, '2'):
            with self.assertRaises(ValueError):
                affinity.validate_cpu_cores(value, 16)

    def test_plan_records_the_selection(self):
        plan = affinity.plan_affinity(2, SMT_8X2)
        self.assertEqual(plan, {'requested_cores': 2, 'logical_cpus': 16, 'physical_cores': 8,
                                'cpus': [0, 2], 'mask': 0b101})
        with self.assertRaises(ValueError):
            affinity.plan_affinity(17, SMT_8X2)


class LauncherWiringTests(unittest.TestCase):
    def test_performance_launcher_has_the_option(self):
        source = (ROOT / 'scripts/play_performance.py').read_text(encoding='utf-8')
        self.assertIn("'--cpu-cores'", source)
        self.assertIn('options.update(cpu_options(args))', source)
        self.assertEqual(performance.cpu_options(type('A', (), {'cpu_cores': None})()), {})
        self.assertEqual(performance.cpu_options(type('A', (), {'cpu_cores': 2})()), {'cpu_cores': 2})

    def test_probe_refuses_outside_performance_modes(self):
        import external_runtime_probe as probe
        with self.assertRaises(ValueError):
            probe.run_probe(ROOT, ROOT / 'private', 1, 1, [], cpu_cores=2, performance=False)

    def test_probe_validates_before_launch(self):
        import external_runtime_probe as probe
        with patch.object(affinity, 'physical_core_topology', return_value=SMT_8X2):
            with self.assertRaises(ValueError):
                probe.run_probe(ROOT, ROOT / 'private', 1, 1, [], cpu_cores=17, performance=True, interactive=True)


if __name__ == '__main__':
    unittest.main()
