import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).parents[1] / "tools"


def run(script, *arguments, check=True):
    return subprocess.run(
        [sys.executable, str(TOOLS / script), *arguments], check=check, capture_output=True, text=True
    )


class DiskImageCliTests(unittest.TestCase):
    def test_wildcards_and_os_image_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "src"
            source.mkdir()
            (source / "A.COM").write_bytes(b"a")
            (source / "B.COM").write_bytes(b"b")
            (source / "notes.text").write_bytes(b"skipped")
            for os_name in ("cpm80", "cpm86"):
                common = ("--os", os_name, "--sd-root", str(root))
                run("diskImage.py", "create", *common, "work.dsk")
                image = root / "retro" / "images" / os_name / "work.dsk"
                self.assertTrue(image.is_file())
                result = run("diskImage.py", "add", *common, "work.dsk", str(source / "*"))
                self.assertIn("skipped", result.stderr)
                listing = run("diskImage.py", "list", *common, "work.dsk").stdout
                self.assertIn("A.COM", listing)
                self.assertIn("B.COM", listing)

    def test_wildcard_without_match_fails(self):
        with tempfile.TemporaryDirectory() as temporary:
            common = ("--os", "cpm80", "--sd-root", temporary)
            run("diskImage.py", "create", *common, "work.dsk")
            result = run("diskImage.py", "add", *common, "work.dsk", f"{temporary}/*.NOPE", check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("matches no files", result.stderr)

    def test_help_is_general_and_os_specific(self):
        general = run("diskImage.py", "-h").stdout
        self.assertIn("apple2", general)
        specific = run("diskImage.py", "--os", "cpm86", "-h").stdout
        self.assertIn("CPM86", specific)
        self.assertIn("--os cpm86", specific)

    def test_planned_os_is_refused(self):
        result = run("diskImage.py", "list", "--os", "apple2", "x.dsk", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not implemented", result.stderr)

    def test_build_cpm80_matches_checked_in_image(self):
        with tempfile.TemporaryDirectory() as temporary:
            run("buildDiskImage.py", "--os", "cpm80", "--sd-root", temporary)
            built = Path(temporary) / "retro" / "images" / "cpm80" / "system.dsk"
            self.assertEqual(built.read_bytes(), (TOOLS.parent / "littlefs" / "cpm80" / "system.dsk").read_bytes())


if __name__ == "__main__":
    unittest.main()
