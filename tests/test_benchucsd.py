import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "include"))
sys.path.insert(0, str(ROOT / "tools" / "apple2"))
sys.path.insert(0, str(ROOT / "tools" / "ucsd"))
import extractUcsd


class BenchUcsdTests(unittest.TestCase):
    def test_source_round_trips_as_ucsd_text(self):
        sourcePath = ROOT / "assets" / "ucsd" / "BENCHUCSD" / "BENCHUCSD.TEXT"
        source = sourcePath.read_bytes()
        self.assertTrue(all(len(line) <= 79 for line in source.splitlines()))
        converted = extractUcsd.hostTextToUcsd(source)
        self.assertEqual(extractUcsd.ucsdTextToHost(converted), source)


if __name__ == "__main__":
    unittest.main()
