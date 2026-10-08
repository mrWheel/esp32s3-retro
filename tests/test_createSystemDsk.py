import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import buildDiskImageCpm80
import createSystemDsk
import diskImageCpm80
import diskImageCpm86

PROJECT_ROOT = Path(createSystemDsk.__file__).resolve().parents[1]


class CreateSystemDskTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.source_dir = self.root / "systemDisk"
        self.source_dir.mkdir()
        self.output_path = self.root / "littlefs" / "cpm80" / "system.dsk"

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_cpm80_small_includes_all_files_with_executables_first(self):
        (self.source_dir / "ZHELP.HLP").write_bytes(b"help")
        (self.source_dir / "ZED.COM").write_bytes(b"program")
        (self.source_dir / "WELCOME.TXT").write_bytes(b"welcome")
        (self.source_dir / "ASM.COM").write_bytes(b"assembler")
        (self.source_dir / "HOST.COM").write_bytes(b"unverified host")

        skipped, overridden = createSystemDsk.create_system_disk(
            "cpm80",
            "SMALL",
            self.output_path,
            source_dir=self.source_dir,
        )

        self.assertEqual(skipped, [])
        self.assertEqual(overridden, ["HOST.COM"])
        self.assertEqual(self.output_path.stat().st_size, diskImageCpm80.IMAGE_SIZE)
        image = self.output_path.read_bytes()
        entries, _, _ = diskImageCpm80.inspect_image(image)
        self.assertEqual(
            [entry["filename"] for entry in entries],
            ["ASM.COM", "HOST.COM", "ZED.COM", "WELCOME.TXT", "ZHELP.HLP"],
        )
        header_offset = buildDiskImageCpm80.CCP_SIZE + buildDiskImageCpm80.BDOS_SIZE
        self.assertEqual(
            image[header_offset : header_offset + len(buildDiskImageCpm80.HEADER)],
            buildDiskImageCpm80.HEADER,
        )
        project_host = PROJECT_ROOT / "guest" / "cpm80" / "host" / "HOST.COM"
        padded_host = project_host.read_bytes().ljust(
            ((project_host.stat().st_size + 127) // 128) * 128, b"\x1a"
        )
        self.assertEqual(diskImageCpm80.extract_file(self.output_path, "HOST.COM"), padded_host)
        self.assertEqual(diskImageCpm80.extract_file(self.output_path, "ZED.COM"), b"program".ljust(128, b"\x1a"))

    def test_cpm80_large_profile_uses_large_geometry(self):
        (self.source_dir / "TOOL.COM").write_bytes(b"tool")
        output = self.root / "large.dsk"

        createSystemDsk.create_system_disk(
            "cpm80",
            "LARGE",
            output,
            source_dir=self.source_dir,
        )

        self.assertEqual(output.stat().st_size, diskImageCpm80.LARGE_IMAGE_SIZE)
        self.assertIn("TOOL.COM", [entry["filename"] for entry in diskImageCpm80.inspect_image(output.read_bytes())[0]])

    def test_non_executable_overflow_keeps_executables_and_reports_skipped_file(self):
        (self.source_dir / "RUNME.COM").write_bytes(b"run")
        (self.source_dir / "MANUAL.HLP").write_bytes(b"x" * diskImageCpm80.IMAGE_SIZE)

        result = subprocess.run(
            [
                sys.executable,
                str(Path(createSystemDsk.__file__)),
                "--os",
                "cpm80",
                "--profile",
                "SMALL",
                "--source-dir",
                str(self.source_dir),
                "--output",
                str(self.output_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("Disk too small", result.stderr)
        self.assertIn("MANUAL.HLP", result.stderr)
        filenames = [entry["filename"] for entry in diskImageCpm80.inspect_image(self.output_path.read_bytes())[0]]
        self.assertIn("RUNME.COM", filenames)
        self.assertIn("HOST.COM", filenames)
        self.assertNotIn("MANUAL.HLP", filenames)

    def test_cpm86_small_preserves_boot_files_and_adds_source_files(self):
        (self.source_dir / "EXTRA.CMD").write_bytes(b"extra command")
        (self.source_dir / "HOST.CMD").write_bytes(b"host command")
        output = self.root / "cpm86-system.dsk"

        createSystemDsk.create_system_disk(
            "cpm86",
            "SMALL",
            output,
            source_dir=self.source_dir,
        )

        image = output.read_bytes()
        filenames = [entry["filename"] for entry in diskImageCpm86.inspect_image(image)[0]]
        self.assertEqual(len(image), diskImageCpm86.IMAGE_SIZE)
        self.assertNotIn("CPM.SYS", filenames)
        self.assertIn("HOST.CMD", filenames)
        self.assertIn("EXTRA.CMD", filenames)
        runtime_system_file = PROJECT_ROOT / "littlefs" / "cpm86" / "cpm.sys"
        self.assertEqual(runtime_system_file.stat().st_size, 10240)
        runtime_bios_overlay = PROJECT_ROOT / "littlefs" / "cpm86" / "retro86bios.h86"
        self.assertTrue(runtime_bios_overlay.is_file())
        self.assertNotIn("RETRO86BIOS.H86", filenames)

    def test_cpm86_large_profile_uses_large_geometry(self):
        output = self.root / "cpm86-large-system.dsk"

        createSystemDsk.create_system_disk(
            "cpm86",
            "LARGE",
            output,
        )

        image = output.read_bytes()
        filenames = [entry["filename"] for entry in diskImageCpm86.inspect_image(image)[0]]
        self.assertEqual(len(image), diskImageCpm86.LARGE_IMAGE_SIZE)
        self.assertIn("ASM86.CMD", filenames)
        self.assertNotIn("CPM.SYS", filenames)
        self.assertIn("HOST.CMD", filenames)

    def test_unimplemented_os_is_rejected(self):
        with self.assertRaisesRegex(createSystemDsk.DiskImageError, "not implemented"):
            createSystemDsk.create_system_disk("ucsd", "SMALL", self.output_path)


if __name__ == "__main__":
    unittest.main()
