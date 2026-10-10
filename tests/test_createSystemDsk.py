import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import buildDiskImageCpm80
import apple2Basic
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

    def test_cli_large_does_not_claim_it_is_unbootable(self):
        for os_name in ("cpm80", "cpm86"):
            output = self.root / f"{os_name}-cli-large.dsk"
            result = subprocess.run(
                [
                    sys.executable,
                    str(Path(createSystemDsk.__file__)),
                    "--os",
                    os_name,
                    "--profile",
                    "LARGE",
                    "--output",
                    str(output),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            combined = (result.stdout + result.stderr).lower()
            self.assertNotIn("only accepts", combined)
            self.assertNotIn("not bootable", combined)
            self.assertTrue(output.is_file())

    def test_all_boot_source_files_are_in_both_profiles(self):
        inspectors = {
            "cpm80": (diskImageCpm80, {"SMALL": diskImageCpm80.IMAGE_SIZE, "LARGE": diskImageCpm80.LARGE_IMAGE_SIZE}),
            "cpm86": (diskImageCpm86, {"SMALL": diskImageCpm86.IMAGE_SIZE, "LARGE": diskImageCpm86.LARGE_IMAGE_SIZE}),
        }
        for os_name, (module, sizes) in inspectors.items():
            source_names = sorted(
                path.name.upper()
                for path in (PROJECT_ROOT / "bootDisks" / os_name / "systemDsk").iterdir()
                if path.is_file() and path.name != ".DS_Store"
            )
            for profile, expected_size in sizes.items():
                with self.subTest(os=os_name, profile=profile):
                    output = self.root / f"{os_name}-{profile}.dsk"
                    skipped, _ = createSystemDsk.create_system_disk(os_name, profile, output)
                    self.assertEqual(skipped, [])
                    image = output.read_bytes()
                    self.assertEqual(len(image), expected_size)
                    present = {entry["filename"] for entry in module.inspect_image(image)[0]}
                    self.assertEqual(sorted(set(source_names) - present), [])

    def test_unimplemented_os_is_rejected(self):
        with self.assertRaisesRegex(createSystemDsk.DiskImageError, "not implemented"):
            createSystemDsk.create_system_disk("ucsd", "SMALL", self.output_path)

    def test_applesoft_token_values_follow_the_rom_token_order(self):
        names = (
            "END FOR NEXT DATA INPUT DEL DIM READ GR TEXT PR# IN# CALL PLOT HLIN VLIN HGR2 HGR HCOLOR= "
            "HPLOT DRAW XDRAW HTAB HOME ROT= SCALE= SHLOAD TRACE NOTRACE NORMAL INVERSE FLASH COLOR= POP "
            "VTAB HIMEM: LOMEM: ONERR RESUME RECALL STORE SPEED= LET GOTO RUN IF RESTORE & GOSUB RETURN "
            "REM STOP ON WAIT LOAD SAVE DEF POKE PRINT CONT LIST CLEAR GET NEW TAB( TO FN SPC( THEN AT "
            "NOT STEP + - * / ^ AND OR > = < SGN INT ABS USR FRE SCRN( PDL POS SQR RND LOG EXP COS SIN "
            "TAN ATN PEEK LEN STR$ VAL ASC CHR$ LEFT$ RIGHT$ MID$"
        ).split()
        table = dict(apple2Basic._TOKENS)
        for index, name in enumerate(names):
            self.assertEqual(table[name], 0x80 + index, name)
        self.assertEqual(table["?"], table["PRINT"])

    def test_applesoft_operators_use_reserved_tokens(self):
        tokenized = apple2Basic.tokenize_source("10 IF A<>1 THEN PRINT 1+2\n")

        self.assertIn(b"\xadA\xd1\xcf1\xc4\xba", tokenized)
        self.assertIn(b"1\xc82", tokenized)

    def test_apple2_prodos_profile_boots_basic_system_and_runs_startup(self):
        import diskImageApple2Prodos as prodos

        output_path = self.root / "apple2-prodos" / "system.dsk"
        createSystemDsk.create_system_disk("apple2", "PRODOS", output_path)
        image = prodos.load_volume(output_path)
        self.assertEqual(len(image), 143360)
        self.assertEqual(prodos.volume_name(image), "SYSTEM")
        names = [entry["name"] for entry in prodos.list_files(image)]
        self.assertEqual(names[:4], list(prodos.BOOT_FILES))
        self.assertIn("STARTUP", names)
        self.assertIn("TEST.NONGR", names)

    def test_apple2_prodos_cli_tells_the_user_to_rebuild_and_flash(self):
        output_path = self.root / "apple2-prodos-cli.dsk"
        result = subprocess.run(
            [sys.executable, str(PROJECT_ROOT / "tools" / "createSystemDsk.py"), "--os", "apple2",
             "--profile", "PRODOS", "--output", str(output_path)],
            check=True, capture_output=True, text=True)
        self.assertIn("rebuilt AND flashed", result.stdout)
        self.assertNotIn("APPLE2_SYSTEM_DISK_PROFILE", result.stdout)


if __name__ == "__main__":
    unittest.main()
