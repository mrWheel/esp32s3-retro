import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).parents[1] / "tools"


def run(*arguments, check=True, input=None, assume_yes=True):
    #-- diskManager.py asks for confirmation before every writing run; scripts pass --yes
    if assume_yes:
        arguments = ("--yes", *arguments)
    return subprocess.run(
        [sys.executable, str(TOOLS / "diskManager.py"), *arguments],
        check=check,
        capture_output=True,
        text=True,
        input=input,
    )


class DiskManagerCliTests(unittest.TestCase):
    def test_work_disk_per_os_default_location_and_size(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = {
                "cpm80": ("work.dsk", "retro/images/cpm80/work.dsk", 256256),
                "cpm86": ("work.dsk", "retro/images/cpm86/work.dsk", 163840),
                "apple2": ("work.po", "retro/images/apple2/work.po", 819200),
                "ucsd": ("work.po", "retro/images/ucsd/work.po", 655360),
            }
            for os_name, (name, relative, size) in expected.items():
                result = run("--type", "WORKDSK", "--os", os_name, "--sd-root", str(root), name)
                self.assertIn("Planned action", result.stdout)
                self.assertIn(f"  type    : WORKDSK", result.stdout)
                image = root / relative
                self.assertTrue(image.is_file(), f"{os_name}: {relative} missing")
                self.assertEqual(image.stat().st_size, size)

    def test_work_disk_refuses_existing_image_without_force(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            common = ("--type", "WORKDSK", "--os", "cpm80", "--sd-root", str(root))
            run(*common, "work.dsk")
            result = run(*common, "work.dsk", check=False)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("use --force", result.stderr)
            run(*common, "--force", "work.dsk")

    def test_writing_runs_require_confirmation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            common = ("--type", "WORKDSK", "--os", "cpm80", "--sd-root", str(root))
            result = run(*common, "work.dsk", check=False, assume_yes=False, input="")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Planned action", result.stdout)
            self.assertIn("  type    : WORKDSK", result.stdout)
            self.assertIn("--yes", result.stderr)
            self.assertFalse((root / "retro" / "images" / "cpm80" / "work.dsk").exists())

    def test_work_disk_needs_an_image_name(self):
        result = run("--type", "WORKDSK", "--os", "cpm80", check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("work disk name or path", result.stderr)

    def test_type_option_is_mandatory(self):
        result = run("--os", "cpm80", check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--type", result.stderr)

    def test_type_accepts_unique_abbreviations_in_any_case(self):
        #-- SYSTEMDSK and WORKDSK differ in their first letter, so every prefix is unique
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for value in ("work", "W", "wo", "WORK"):
                result = run("--type", value, "--os", "cpm80", "--sd-root", str(root),
                             "work.dsk", check=False)
                self.assertIn("  type    : WORKDSK", result.stdout, f"--type {value}")
            output = Path(root) / "system.dsk"
            result = run("--type", "sys", "--os", "cpm80", "--profile", "CPM80_512K",
                         "--output", str(output), check=False)
            self.assertIn("  type    : SYSTEMDSK", result.stdout)
            self.assertTrue(output.is_file())

    def test_type_rejects_values_outside_the_two_types(self):
        for value in ("X", "DISK", "SYSTEMWORK"):
            result = run("--type", value, "--os", "cpm80", "work.dsk", check=False)
            self.assertEqual(result.returncode, 2, f"--type {value}")
            self.assertIn("SYSTEMDSK or WORKDSK", result.stderr)

    def test_system_disk_builds_bootable_image_with_summary(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "system.dsk"
            result = run("--type", "SYSTEMDSK", "--os", "cpm80", "--profile", "CPM80_512K",
                         "--output", str(output))
            self.assertIn("Planned action", result.stdout)
            self.assertIn("  type    : SYSTEMDSK", result.stdout)
            self.assertIn("  effect  : build a bootable system disk", result.stdout)
            self.assertTrue(output.is_file())
            self.assertEqual(output.stat().st_size, 512512)
            #-- the RETROCPM header follows CCP (0x800) and BDOS (0xE00)
            self.assertEqual(output.read_bytes().find(b"RETROCPM"), 0x800 + 0xE00)


if __name__ == "__main__":
    unittest.main()
