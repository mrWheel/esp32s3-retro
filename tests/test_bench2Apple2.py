import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "assets" / "apple2" / "BENCH2"
TOOLS = ROOT / "tools" / "apple2"
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(ROOT / "tools"))
import apple2Basic
import diskImageApple2Prodos


class Bench2Apple2Tests(unittest.TestCase):
    def test_build_matches_tokenized_source(self):
        source = (PACKAGE / "BENCH2.SRC").read_text(encoding="ascii")
        expected = apple2Basic.tokenize_source(source)
        subprocess.run([sys.executable, str(PACKAGE / "build.py")], check=True)
        actual = (PACKAGE / "BENCH2.BAS").read_bytes()
        self.assertEqual(actual, expected)
        self.assertFalse(apple2Basic.is_ascii_source(actual))

    def test_prodos_source_file_metadata(self):
        source = (PACKAGE / "BENCH2.SRC").read_text(encoding="ascii")
        program = apple2Basic.tokenize_source(source)
        name, fileType, auxType, payload = diskImageApple2Prodos.prepare_source_file(
            "BENCH2.BAS", program
        )
        self.assertEqual((name, fileType, auxType), ("BENCH2", 0xFC, 0x0801))
        self.assertEqual(payload, program)


if __name__ == "__main__":
    unittest.main()
