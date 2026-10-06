import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import cpm80DiskImage


class Cpm80DiskImageTests(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.image_path = Path(self.temporary_directory.name) / "work.dsk"
        cpm80DiskImage.create_image(self.image_path)

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_create_formats_exact_project_geometry(self):
        image = self.image_path.read_bytes()
        self.assertEqual(len(image), cpm80DiskImage.IMAGE_SIZE)
        self.assertTrue(
            all(
                value == cpm80DiskImage.EMPTY
                for value in image[
                    cpm80DiskImage.DIRECTORY_OFFSET : cpm80DiskImage.DIRECTORY_OFFSET
                    + cpm80DiskImage.DIRECTORY_SIZE
                ]
            )
        )
        entries, free_slots, used_blocks = cpm80DiskImage.inspect_image(image)
        self.assertEqual(entries, [])
        self.assertEqual(len(free_slots), cpm80DiskImage.DIRECTORY_ENTRIES)
        self.assertEqual(used_blocks, set())

    def test_large_profile_uses_double_density_geometry_and_adds_files(self):
        large_path = Path(self.temporary_directory.name) / "large.dsk"
        cpm80DiskImage.create_image(large_path, profile="LARGE")
        image = large_path.read_bytes()
        self.assertEqual(len(image), cpm80DiskImage.LARGE_IMAGE_SIZE)
        self.assertTrue(
            all(
                value == cpm80DiskImage.EMPTY
                for value in image[
                    cpm80DiskImage.LARGE_DIRECTORY_OFFSET : cpm80DiskImage.LARGE_DIRECTORY_OFFSET
                    + cpm80DiskImage.LARGE_DIRECTORY_SIZE
                ]
            )
        )
        self.assertEqual(len(cpm80DiskImage.inspect_image(image)[1]), cpm80DiskImage.LARGE_DIRECTORY_ENTRIES)
        payload = bytes(range(256)) * 100
        cpm80DiskImage.add_files(large_path, [("LARGE.COM", payload)])
        entries, _, _ = cpm80DiskImage.inspect_image(large_path.read_bytes())
        self.assertEqual([entry["filename"] for entry in entries], ["LARGE.COM", "LARGE.COM"])
        self.assertEqual([entry["extent"] for entry in entries], [0, 1])

    def test_install_validated_raw_image_refuses_unrequested_overwrite(self):
        destination = self.image_path.parent / "copy.dsk"
        data = self.image_path.read_bytes()
        cpm80DiskImage.write_image(destination, data)
        self.assertEqual(destination.read_bytes(), data)
        with self.assertRaisesRegex(cpm80DiskImage.DiskImageError, "already exists"):
            cpm80DiskImage.write_image(destination, data)
        cpm80DiskImage.write_image(destination, data, force=True)

    def test_add_file_round_trips_records_and_multiple_extents(self):
        data = bytes(range(256)) * 100
        cpm80DiskImage.add_files(self.image_path, [("mbasic.com", data)])
        image = self.image_path.read_bytes()
        entries, _, _ = cpm80DiskImage.inspect_image(image)
        file_entries = [entry for entry in entries if entry["filename"] == "MBASIC.COM"]
        self.assertEqual([entry["extent"] for entry in file_entries], [0, 1])

        recovered = bytearray()
        for entry in file_entries:
            extent_data = bytearray()
            for block in entry["blocks"]:
                offset = cpm80DiskImage.DIRECTORY_OFFSET + block * cpm80DiskImage.BLOCK_SIZE
                extent_data.extend(image[offset : offset + cpm80DiskImage.BLOCK_SIZE])
            recovered.extend(extent_data[: entry["records"] * cpm80DiskImage.SECTOR_SIZE])

        padded_data = data.ljust(
            ((len(data) + cpm80DiskImage.SECTOR_SIZE - 1) // cpm80DiskImage.SECTOR_SIZE)
            * cpm80DiskImage.SECTOR_SIZE,
            b"\x1A",
        )
        self.assertEqual(recovered, padded_data)

    def test_duplicate_and_disk_full_errors_do_not_change_image(self):
        cpm80DiskImage.add_files(self.image_path, [("ONE.COM", b"one")])
        initial = self.image_path.read_bytes()
        with self.assertRaises(cpm80DiskImage.DiskImageError):
            cpm80DiskImage.add_files(self.image_path, [("ONE.COM", b"replacement")])
        self.assertEqual(self.image_path.read_bytes(), initial)

        oversized_data = bytes(cpm80DiskImage.IMAGE_SIZE)
        with self.assertRaisesRegex(cpm80DiskImage.DiskImageError, "disk is full"):
            cpm80DiskImage.add_files(self.image_path, [("TOOBIG.COM", oversized_data)])
        self.assertEqual(self.image_path.read_bytes(), initial)

    def test_create_refuses_overwrite_without_force(self):
        initial = self.image_path.read_bytes()
        with self.assertRaisesRegex(cpm80DiskImage.DiskImageError, "already exists"):
            cpm80DiskImage.create_image(self.image_path)
        self.assertEqual(self.image_path.read_bytes(), initial)
        cpm80DiskImage.create_image(self.image_path, force=True)

    def test_invalid_geometry_and_filename_are_rejected(self):
        with self.assertRaisesRegex(cpm80DiskImage.DiskImageError, "expected"):
            cpm80DiskImage.inspect_image(b"not a disk")
        with self.assertRaisesRegex(cpm80DiskImage.DiskImageError, "8.3"):
            cpm80DiskImage.parse_filename("../BAD.COM")

    def test_command_line_creates_and_populates_work_image(self):
        cli = Path(cpm80DiskImage.__file__)
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
