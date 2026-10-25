import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import createSystemDsk

#-- every bench program must be in bootDisks/<os>/systemDsk exactly as built in assets/
BENCH_FILES = (
    ("apple2", "BENCH2.BAS", "assets/apple2/BENCH2/BENCH2.BAS"),
    ("apple2", "BENCH2ASM.BIN", "assets/apple2/BENCH2ASM/BENCH2ASM.BIN"),
    ("cpm80", "BENCH80.COM", "assets/cpm80/BENCH80/BENCH80.COM"),
    ("cpm86", "BENCH86.CMD", "assets/cpm86/BENCH86/BENCH86.CMD"),
    ("ucsd", "BENCHUCSD.TEXT", "assets/ucsd/BENCHUCSD/BENCHUCSD.TEXT"),
)


class BootDisksBenchTests(unittest.TestCase):
    def test_boot_disk_copies_match_assets(self):
        for osName, name, asset in BENCH_FILES:
            with self.subTest(file=name):
                copy = ROOT / "bootDisks" / osName / "systemDsk" / name
                self.assertEqual(copy.read_bytes(), (ROOT / asset).read_bytes())

    def test_system_disks_generate(self):
        profiles = (("apple2", "PRODOS_640K"), ("cpm80", "LARGE"), ("cpm86", "LARGE"), ("ucsd", "PASCAL_800K"))
        with tempfile.TemporaryDirectory() as temporary:
            for osName, profile in profiles:
                with self.subTest(os=osName):
                    output = pathlib.Path(temporary) / f"{osName}.img"
                    self.assertEqual(createSystemDsk.create_system_disk(osName, profile, output), [])
                    self.assertGreater(output.stat().st_size, 0)

    def test_ucsd_disk_contains_bench_source(self):
        sys.path.insert(0, str(ROOT / "tools" / "apple2"))
        import extractApple2Files
        with tempfile.TemporaryDirectory() as temporary:
            output = pathlib.Path(temporary) / "system.po"
            createSystemDsk.create_system_disk("ucsd", "PASCAL_800K", output)
            volume = extractApple2Files.openVolume(extractApple2Files.DiskImage(output))
            self.assertIn("BENCHUCSD.TEXT", [entry.path for entry in volume.listFiles()])


if __name__ == "__main__":
    unittest.main()
