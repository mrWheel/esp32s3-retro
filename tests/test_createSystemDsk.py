import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import buildDiskImageCpm80
import apple2Basic
import createSystemDsk
import diskImageApple2Dos33
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

    def _create_apple2_base_image(self, path):
        image = bytearray(diskImageApple2Dos33.IMAGE_SIZE)
        vtoc_offset = diskImageApple2Dos33._sector_offset(
            diskImageApple2Dos33.VTOC_TRACK,
            diskImageApple2Dos33.VTOC_SECTOR,
        )
        vtoc = bytearray(256)
        vtoc[1] = diskImageApple2Dos33.VTOC_TRACK
        vtoc[2] = 15
        vtoc[3] = 3
        vtoc[6] = 254
        vtoc[0x27] = diskImageApple2Dos33.MAX_TS_PAIRS_PER_LIST
        vtoc[0x34] = diskImageApple2Dos33.TRACKS
        vtoc[0x35] = diskImageApple2Dos33.SECTORS_PER_TRACK
        vtoc[0x36:0x38] = (256).to_bytes(2, "little")
        for track in range(diskImageApple2Dos33.TRACKS):
            for sector in range(diskImageApple2Dos33.SECTORS_PER_TRACK):
                diskImageApple2Dos33._set_free(vtoc, track, sector, True)
        for track in (0, 1, 2):
            for sector in range(diskImageApple2Dos33.SECTORS_PER_TRACK):
                diskImageApple2Dos33._set_free(vtoc, track, sector, False)
        for sector in (0, 15):
            diskImageApple2Dos33._set_free(vtoc, 17, sector, False)
        image[vtoc_offset : vtoc_offset + 256] = vtoc
        catalog_offset = diskImageApple2Dos33._sector_offset(17, 15)
        image[catalog_offset : catalog_offset + 256] = bytes(256)
        path.write_bytes(image)

    def test_apple2_adds_dos33_files_without_changing_base_image(self):
        source_dir = self.root / "apple2-systemDsk"
        source_dir.mkdir()
        (source_dir / "HELLO.TXT").write_bytes(b"HELLO\r")
        (source_dir / "START.BIN").write_bytes(b"\xA9\x00\x60")
        base_image = self.root / "dos33-base.do"
        output_path = self.root / "littlefs" / "apple2" / "system.dsk"
        self._create_apple2_base_image(base_image)
        original_base = base_image.read_bytes()

        skipped, overridden = createSystemDsk.create_system_disk(
            "apple2",
            "DOS33",
            output_path,
            source_dir=source_dir,
            base_image=base_image,
            binary_load_address=0x800,
        )

        self.assertEqual((skipped, overridden), ([], []))
        self.assertEqual(base_image.read_bytes(), original_base)
        image = output_path.read_bytes()
        self.assertEqual(len(image), diskImageApple2Dos33.IMAGE_SIZE)
        entries = diskImageApple2Dos33.inspect_image(image)
        self.assertEqual(entries, ["START.BIN", "HELLO.TXT"])

        catalog_offset = diskImageApple2Dos33._sector_offset(17, 15)
        catalog_entries = {}
        for slot in range(7):
            entry_offset = catalog_offset + 0x0B + slot * 35
            if image[entry_offset] in (0, 0xFF):
                continue
            name_bytes = image[entry_offset + 3 : entry_offset + 33]
            name = bytes(value & 0x7F for value in name_bytes).decode("ascii").rstrip()
            catalog_entries[name] = image[entry_offset : entry_offset + 35]
        text_entry = catalog_entries["HELLO.TXT"]
        self.assertEqual(text_entry[2], 0)
        self.assertEqual(text_entry[33:35], (2).to_bytes(2, "little"))
        text_list_offset = diskImageApple2Dos33._sector_offset(text_entry[0], text_entry[1])
        text_track, text_sector = image[text_list_offset + 0x0C : text_list_offset + 0x0E]
        text_data_offset = diskImageApple2Dos33._sector_offset(text_track, text_sector)
        self.assertEqual(image[text_data_offset : text_data_offset + 6], b"HELLO\r")

        binary_entry = catalog_entries["START.BIN"]
        self.assertEqual(binary_entry[2], 4)
        binary_list_offset = diskImageApple2Dos33._sector_offset(binary_entry[0], binary_entry[1])
        binary_track, binary_sector = image[binary_list_offset + 0x0C : binary_list_offset + 0x0E]
        binary_data_offset = diskImageApple2Dos33._sector_offset(binary_track, binary_sector)
        self.assertEqual(image[binary_data_offset : binary_data_offset + 7], b"\x00\x08\x03\x00\xA9\x00\x60")

    def test_apple2_requires_binary_load_address_and_rejects_duplicate_names(self):
        base_image = self.root / "dos33-base.do"
        self._create_apple2_base_image(base_image)
        image = base_image.read_bytes()
        with self.assertRaisesRegex(createSystemDsk.DiskImageError, "load address is required"):
            diskImageApple2Dos33.add_files(image, [("START.BIN", b"\x60")])
        with self.assertRaisesRegex(createSystemDsk.DiskImageError, "already exists"):
            diskImageApple2Dos33.add_files(
                image,
                [("HELLO.TXT", b"one"), ("HELLO.TXT", b"two")],
            )
        self.assertEqual(base_image.read_bytes(), image)

    def test_apple2_text_source_uses_dos_carriage_return_lines(self):
        file_type, payload = diskImageApple2Dos33._file_payload(
            "TEST.TXT",
            b"10 PRINT 1\n20 END\n",
            None,
        )

        self.assertEqual(file_type, 0)
        self.assertEqual(payload, b"10 PRINT 1\r20 END\r")

    def test_apple2_applesoft_source_is_tokenized_for_dos_load(self):
        file_type, payload = diskImageApple2Dos33._file_payload(
            "HELLO.BAS",
            b'20 PRINT "HI"\n10 REM COMMENT\n',
            None,
        )

        self.assertEqual(file_type, 2)
        self.assertEqual(
            payload,
            b"\x1a\x00"
            b"\x0f\x08\x0a\x00\xb2 COMMENT\x00"
            b"\x19\x08\x14\x00\xba\x22HI\x22\x00\x00\x00",
        )

    def test_tagged_applesoft_program_tokenizes_for_dos_load(self):
        source = (PROJECT_ROOT / "bootDisks" / "apple2" / "systemDsk" / "TEST-NONGR.BAS").read_bytes()

        file_type, payload = diskImageApple2Dos33._file_payload("TEST-NONGR.BAS", source, None)

        self.assertEqual(file_type, 2)
        self.assertEqual(int.from_bytes(payload[0:2], "little"), len(payload) - 2)
        self.assertEqual(payload[4:6], b"\x0a\x00")
        self.assertTrue(payload.endswith(b"\x00\x00"))
        self.assertIn(b"\xba", payload)
        self.assertIn(b"\xb2", payload)

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

    def test_apple2_applesoft_filename_uses_extensionless_dos_catalog_name(self):
        base_image = self.root / "dos33-bas-name-base.do"
        self._create_apple2_base_image(base_image)
        source = (PROJECT_ROOT / "bootDisks" / "apple2" / "systemDsk" / "TEST-NONGR.BAS").read_bytes()

        image = diskImageApple2Dos33.add_files(
            base_image.read_bytes(),
            [("TEST-NONGR.BAS", source)],
        )

        self.assertEqual(diskImageApple2Dos33.inspect_image(image), ["TEST-NONGR"])
        catalog_offset = diskImageApple2Dos33._sector_offset(17, 15)
        entry = image[catalog_offset + 0x0B : catalog_offset + 0x0B + 35]
        self.assertEqual(entry[2], diskImageApple2Dos33.FILE_TYPES[".BAS"])

    def test_apple2_removes_only_requested_unlocked_file_and_reclaims_sectors(self):
        base_image = self.root / "dos33-remove-base.do"
        self._create_apple2_base_image(base_image)
        image = bytearray(base_image.read_bytes())
        vtoc_offset, vtoc, catalog_track, catalog_sector = diskImageApple2Dos33._read_vtoc(image)
        diskImageApple2Dos33._write_file(
            image,
            vtoc,
            "LOCKSMITH 4.1",
            b"old file",
            diskImageApple2Dos33.FILE_TYPES[".TXT"],
            catalog_track,
            catalog_sector,
        )
        image[vtoc_offset : vtoc_offset + 256] = vtoc
        original_free = sum(
            diskImageApple2Dos33._is_free(vtoc, track, sector)
            for track in range(diskImageApple2Dos33.TRACKS)
            for sector in range(diskImageApple2Dos33.SECTORS_PER_TRACK)
        )

        removed = diskImageApple2Dos33.remove_files(bytes(image), ["LOCKSMITH 4.1"])

        _, removed_vtoc, _, _ = diskImageApple2Dos33._read_vtoc(removed)
        removed_free = sum(
            diskImageApple2Dos33._is_free(removed_vtoc, track, sector)
            for track in range(diskImageApple2Dos33.TRACKS)
            for sector in range(diskImageApple2Dos33.SECTORS_PER_TRACK)
        )
        self.assertEqual(diskImageApple2Dos33.inspect_image(removed), [])
        self.assertEqual(removed_free, original_free + 2)
        self.assertEqual(diskImageApple2Dos33.inspect_image(bytes(image)), ["LOCKSMITH 4.1"])

    def test_apple2_refuses_to_remove_locked_file(self):
        base_image = self.root / "dos33-locked-base.do"
        self._create_apple2_base_image(base_image)
        image = bytearray(base_image.read_bytes())
        vtoc_offset, vtoc, catalog_track, catalog_sector = diskImageApple2Dos33._read_vtoc(image)
        diskImageApple2Dos33._write_file(
            image,
            vtoc,
            "LOCKSMITH 4.1",
            b"old file",
            diskImageApple2Dos33.FILE_TYPES[".TXT"],
            catalog_track,
            catalog_sector,
        )
        image[vtoc_offset : vtoc_offset + 256] = vtoc
        entry_offset = diskImageApple2Dos33._sector_offset(17, 15) + 0x0B
        image[entry_offset + 2] |= 0x80

        with self.assertRaisesRegex(createSystemDsk.DiskImageError, "locked"):
            diskImageApple2Dos33.remove_files(bytes(image), ["LOCKSMITH 4.1"])

    def test_apple2_system_disk_reclaims_selected_base_file_before_adding_program(self):
        source_dir = self.root / "apple2-remove-source"
        source_dir.mkdir()
        (source_dir / "TEST-NONGR.BAS").write_bytes(b"10 PRINT 2+2\n")
        base_image = self.root / "dos33-remove-system-base.do"
        output_path = self.root / "apple2-remove-system.dsk"
        self._create_apple2_base_image(base_image)
        image = bytearray(base_image.read_bytes())
        vtoc_offset, vtoc, catalog_track, catalog_sector = diskImageApple2Dos33._read_vtoc(image)
        diskImageApple2Dos33._write_file(
            image,
            vtoc,
            "LOCKSMITH 4.1",
            b"old file",
            diskImageApple2Dos33.FILE_TYPES[".TXT"],
            catalog_track,
            catalog_sector,
        )
        image[vtoc_offset : vtoc_offset + 256] = vtoc
        base_image.write_bytes(image)
        original_base = base_image.read_bytes()

        createSystemDsk.create_system_disk(
            "apple2",
            "DOS33",
            output_path,
            source_dir=source_dir,
            base_image=base_image,
            remove_existing_files=["LOCKSMITH 4.1"],
        )

        self.assertEqual(base_image.read_bytes(), original_base)
        result = output_path.read_bytes()
        self.assertEqual(diskImageApple2Dos33.inspect_image(result), ["TEST-NONGR"])
        self.assertEqual(result[diskImageApple2Dos33._sector_offset(17, 15) + 0x0B + 2], 2)

    def test_applesoft_operators_use_reserved_tokens(self):
        tokenized = apple2Basic.tokenize_source("10 IF A<>1 THEN PRINT 1+2\n")

        self.assertIn(b"\xadA\xd1\xcf1\xc4\xba", tokenized)
        self.assertIn(b"1\xc82", tokenized)

    def test_apple2_large_file_uses_chained_track_sector_lists(self):
        base_image = self.root / "dos33-large-base.do"
        self._create_apple2_base_image(base_image)
        payload = bytes(index % 251 for index in range(122 * 256 + 1))

        image = diskImageApple2Dos33.add_files(
            base_image.read_bytes(),
            [("LARGE.BAS", payload)],
        )

        catalog_offset = diskImageApple2Dos33._sector_offset(17, 15)
        entry = image[catalog_offset + 0x0B : catalog_offset + 0x0B + 35]
        self.assertEqual(entry[33:35], (125).to_bytes(2, "little"))
        first_list = diskImageApple2Dos33._sector_offset(entry[0], entry[1])
        self.assertEqual(image[first_list + 5 : first_list + 7], b"\x00\x00")
        second_track, second_sector = image[first_list + 1 : first_list + 3]
        second_list = diskImageApple2Dos33._sector_offset(second_track, second_sector)
        self.assertEqual(image[second_list + 5 : second_list + 7], (122).to_bytes(2, "little"))
        self.assertEqual(image[second_list + 1 : second_list + 3], b"\x00\x00")

    def test_apple2_default_base_is_the_empty_dos_image(self):
        source_dir = self.root / "apple2-default-base-source"
        source_dir.mkdir()
        (source_dir / "NEW.TXT").write_bytes(b"new\n")
        output_path = self.root / "default-base.dsk"

        createSystemDsk.create_system_disk("apple2", "DOS33", output_path, source_dir=source_dir)

        self.assertEqual(diskImageApple2Dos33.inspect_image(output_path.read_bytes()), ["NEW.TXT"])

    def test_apple2_cli_adds_files_to_supplied_base(self):
        source_dir = self.root / "apple2-cli-source"
        source_dir.mkdir()
        (source_dir / "HELLO.TXT").write_bytes(b"HELLO\r")
        base_image = self.root / "dos33-cli-base.do"
        output_path = self.root / "apple2-cli-output.dsk"
        self._create_apple2_base_image(base_image)

        result = subprocess.run(
            [
                sys.executable,
                str(Path(createSystemDsk.__file__)),
                "--os",
                "apple2",
                "--profile",
                "DOS33",
                "--source-dir",
                str(source_dir),
                "--base-image",
                str(base_image),
                "--output",
                str(output_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("bootability depends on that image", result.stdout)
        self.assertEqual(diskImageApple2Dos33.inspect_image(output_path.read_bytes()), ["HELLO.TXT"])


if __name__ == "__main__":
    unittest.main()
