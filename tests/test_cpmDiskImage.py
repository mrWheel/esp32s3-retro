import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import cpmDiskImage


class CpmDiskImageTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.image_path = Path(self.temporary_directory.name) / "work.dsk"
        cpmDiskImage.create_image(self.image_path)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_create_formats_exact_project_geometry(self):
        image = self.image_path.read_bytes()
        self.assertEqual(len(image), cpmDiskImage.IMAGE_SIZE)
        self.assertTrue(
            all(
                value == cpmDiskImage.EMPTY
                for value in image[
                    cpmDiskImage.DIRECTORY_OFFSET : cpmDiskImage.DIRECTORY_OFFSET
                    + cpmDiskImage.DIRECTORY_SIZE
                ]
            )
        )
        entries, free_slots, used_blocks = cpmDiskImage.inspect_image(image)
        self.assertEqual(entries, [])
        self.assertEqual(len(free_slots), cpmDiskImage.DIRECTORY_ENTRIES)
        self.assertEqual(used_blocks, set())

    def test_install_validated_raw_image_refuses_unrequested_overwrite(self):
        destination = self.image_path.parent / "copy.dsk"
        data = self.image_path.read_bytes()
        cpmDiskImage.write_image(destination, data)
        self.assertEqual(destination.read_bytes(), data)
        with self.assertRaisesRegex(cpmDiskImage.DiskImageError, "already exists"):
            cpmDiskImage.write_image(destination, data)
        cpmDiskImage.write_image(destination, data, force=True)

    def test_add_file_round_trips_records_and_multiple_extents(self):
        data = bytes(range(256)) * 100
        cpmDiskImage.add_files(self.image_path, [("mbasic.com", data)])
        image = self.image_path.read_bytes()
        entries, _, _ = cpmDiskImage.inspect_image(image)
        file_entries = [entry for entry in entries if entry["filename"] == "MBASIC.COM"]
        self.assertEqual([entry["extent"] for entry in file_entries], [0, 1])

        recovered = bytearray()
        for entry in file_entries:
            extent_data = bytearray()
            for block in entry["blocks"]:
                offset = cpmDiskImage.DIRECTORY_OFFSET + block * cpmDiskImage.BLOCK_SIZE
                extent_data.extend(image[offset : offset + cpmDiskImage.BLOCK_SIZE])
            recovered.extend(extent_data[: entry["records"] * cpmDiskImage.SECTOR_SIZE])

        padded_data = data.ljust(
            ((len(data) + cpmDiskImage.SECTOR_SIZE - 1) // cpmDiskImage.SECTOR_SIZE)
            * cpmDiskImage.SECTOR_SIZE,
            b"\x1A",
        )
        self.assertEqual(recovered, padded_data)

    def test_duplicate_and_disk_full_errors_do_not_change_image(self):
        cpmDiskImage.add_files(self.image_path, [("ONE.COM", b"one")])
        initial = self.image_path.read_bytes()
        with self.assertRaises(cpmDiskImage.DiskImageError):
            cpmDiskImage.add_files(self.image_path, [("ONE.COM", b"replacement")])
        self.assertEqual(self.image_path.read_bytes(), initial)

        oversized_data = bytes(cpmDiskImage.IMAGE_SIZE)
        with self.assertRaisesRegex(cpmDiskImage.DiskImageError, "disk is full"):
            cpmDiskImage.add_files(self.image_path, [("TOOBIG.COM", oversized_data)])
        self.assertEqual(self.image_path.read_bytes(), initial)

    def test_create_refuses_overwrite_without_force(self):
        initial = self.image_path.read_bytes()
        with self.assertRaisesRegex(cpmDiskImage.DiskImageError, "already exists"):
            cpmDiskImage.create_image(self.image_path)
        self.assertEqual(self.image_path.read_bytes(), initial)
        cpmDiskImage.create_image(self.image_path, force=True)

    def test_invalid_geometry_and_filename_are_rejected(self):
        with self.assertRaisesRegex(cpmDiskImage.DiskImageError, "expected"):
            cpmDiskImage.inspect_image(b"not a disk")
        with self.assertRaisesRegex(cpmDiskImage.DiskImageError, "8.3"):
            cpmDiskImage.parse_filename("../BAD.COM")

    def test_command_line_creates_and_populates_work_image(self):
        cli = Path(cpmDiskImage.__file__)
        source = self.image_path.parent / "MBASIC.COM"
        source.write_bytes(b"local interpreter binary")
        self.image_path.unlink()
        subprocess.run(
            [sys.executable, str(cli), "create", str(self.image_path)],
            check=True,
            capture_output=True,
            text=True,
        )
        subprocess.run(
            [sys.executable, str(cli), "add", str(self.image_path), str(source)],
            check=True,
            capture_output=True,
            text=True,
        )
        output = subprocess.run(
            [sys.executable, str(cli), "list", str(self.image_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        self.assertIn("MBASIC.COM", output)


if __name__ == "__main__":
    unittest.main()
