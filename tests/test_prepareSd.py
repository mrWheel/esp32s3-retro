import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class PrepareSdTests(unittest.TestCase):
    def test_creates_default_cpm_drives_without_overwriting_edits(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            config_path = root / "retro" / "images" / "cpm" / "drives.cfg"
            script = Path(__file__).parents[1] / "tools" / "prepareSd.py"
            subprocess.run([sys.executable, str(script), str(root)], check=True, capture_output=True, text=True)
            self.assertIn("A=/littlefs/cpm/system.dsk,RO,SYSTEM", config_path.read_text())
            self.assertIn("F=/retro/images/cpm/archive.dsk,RW,LARGE", config_path.read_text())

            config_path.write_text("custom config\n")
            subprocess.run([sys.executable, str(script), str(root)], check=True, capture_output=True, text=True)
            self.assertEqual(config_path.read_text(), "custom config\n")


if __name__ == "__main__":
    unittest.main()
