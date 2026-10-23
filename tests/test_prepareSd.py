import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class PrepareSdTests(unittest.TestCase):
    def test_creates_layout_without_drive_configuration(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            script = Path(__file__).parents[1] / "tools" / "prepareSd.py"
            subprocess.run([sys.executable, str(script), str(root)], check=True, capture_output=True, text=True)
            self.assertTrue((root / "retro" / "layout.txt").is_file())
            self.assertTrue((root / "retro" / "images" / "cpm80").is_dir())
            self.assertEqual(list(root.rglob("drives.cfg")), [])


if __name__ == "__main__":
    unittest.main()
