import unittest
from pathlib import Path
from verify_partition_layout import EXPECTED, read_layout, validate_layout, size_health

class LayoutTests(unittest.TestCase):
    def test_actual_production_contract(self):
        parts = read_layout(Path(__file__).resolve().parents[1] / 'partitions.factory.csv')
        self.assertEqual(validate_layout(parts), 0x800000)
        self.assertEqual(parts['nvs'][2], 0x9000)
        self.assertEqual(sum(p[1].startswith('ota_') for p in parts.values()), 2)

    def test_invalid_layouts(self):
        for key, value in [('factory', ('app','factory',0x10001,0x250000)),
                           ('ota_0', ('app','ota_0',0x250000,0x250000)),
                           ('nvs', ('data','nvs',0xa000,0x4000))]:
            parts = dict(EXPECTED); parts[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): validate_layout(parts)
        for missing in ('factory','otadata','ota_0','ota_1'):
            parts = dict(EXPECTED); del parts[missing]
            with self.subTest(missing=missing), self.assertRaises(ValueError): validate_layout(parts)
        with self.assertRaises(ValueError): validate_layout(EXPECTED, 0x400000)
        with self.assertRaises(ValueError): validate_layout(EXPECTED, version=1)

    def test_size_boundaries(self):
        self.assertEqual(size_health(749,1000), 'HEALTHY')
        self.assertEqual(size_health(750,1000), 'WARNING')
        self.assertEqual(size_health(850,1000), 'WARNING')
        self.assertEqual(size_health(851,1000), 'FAIL')
        self.assertEqual(size_health(1300000, EXPECTED['factory'][3]), 'HEALTHY')

if __name__ == '__main__': unittest.main()
