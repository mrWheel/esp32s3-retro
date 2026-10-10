import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import diskImageApple2Prodos as prodos
from diskImageCommon import DiskImageError

PROJECT_ROOT = Path(prodos.__file__).resolve().parents[1]


class BlankProdosVolumeTests(unittest.TestCase):
    def test_800k_is_the_default_profile(self):
        image = prodos.build_blank_volume("data800")
        self.assertEqual(len(image), 819200)
        self.assertEqual(len(image) // prodos.BLOCK_SIZE, 1600)
        header = 2 * prodos.BLOCK_SIZE
        self.assertEqual(image[header + 4], 0xF7)
        self.assertEqual(image[header + 5:header + 12], b"DATA800")
        self.assertEqual(struct.unpack_from("<HHH", image, header + 0x25), (0, 6, 1600))

    def test_640k_geometry_and_header(self):
        image = prodos.build_blank_volume("data640", "640K")
        self.assertEqual(len(image), 655360)
        self.assertEqual(len(image) // prodos.BLOCK_SIZE, 1280)
        header = 2 * prodos.BLOCK_SIZE
        self.assertEqual(image[header + 4], 0xF7)
        self.assertEqual(image[header + 5:header + 12], b"DATA640")
        self.assertEqual(image[header + 0x22], 0xC3)
        self.assertEqual(image[header + 0x23], 0x27)
        self.assertEqual(image[header + 0x24], 13)
        self.assertEqual(struct.unpack_from("<HHH", image, header + 0x25), (0, 6, 1280))

    def test_directory_chain_and_free_block_bitmap(self):
        image = prodos.build_blank_volume("WORK", "640K")
        for block in range(2, 6):
            previous, following = struct.unpack_from("<HH", image, block * prodos.BLOCK_SIZE)
            self.assertEqual(previous, block - 1 if block > 2 else 0)
            self.assertEqual(following, block + 1 if block < 5 else 0)

        bitmap = image[6 * prodos.BLOCK_SIZE:7 * prodos.BLOCK_SIZE]

        def is_free(block):
            return bool(bitmap[block // 8] & (0x80 >> (block & 7)))

        self.assertTrue(all(not is_free(block) for block in range(7)))
        self.assertTrue(is_free(7))
        self.assertTrue(is_free(1279))
        self.assertFalse(is_free(1280))

    def test_140k_volume(self):
        image = prodos.build_blank_volume("SMALL", "140K")
        self.assertEqual(len(image), 143360)
        header = 2 * prodos.BLOCK_SIZE
        self.assertEqual(struct.unpack_from("<H", image, header + 0x29)[0], 280)

    def test_name_rules(self):
        for bad in ("", "1DATA", "TOOLONGVOLUMENAME", "A B", "A=B", "A/"):
            with self.assertRaises(DiskImageError):
                prodos.build_blank_volume(bad)

    def test_unknown_profile(self):
        with self.assertRaises(DiskImageError):
            prodos.build_blank_volume("WORK", "999K")

    def test_command_line_writes_file(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "blank.po"
            self.assertEqual(prodos.main([str(target), "--name", "DATA", "--profile", "140K"]), 0)
            self.assertEqual(target.stat().st_size, 143360)
            self.assertEqual(prodos.main([str(target), "--name", "BAD NAME"]), 1)


class ProdosFileTests(unittest.TestCase):
    def test_add_and_read_back_seedling_sapling_and_tree(self):
        image = bytearray(prodos.build_blank_volume("WORK", "800K"))
        payloads = {"SMALL": b"a" * 100, "MEDIUM": bytes(range(256)) * 20, "LARGE": b"xyz" * 100000}
        for name, payload in payloads.items():
            prodos.add_file(image, name, payload, 0x06, 0x2000)
        for name, payload in payloads.items():
            data, entry = prodos.read_file(image, name)
            self.assertEqual(data, payload)
        self.assertEqual({entry["name"]: entry["storage"] for entry in prodos.list_files(image)},
                         {"SMALL": 1, "MEDIUM": 2, "LARGE": 3})
        self.assertEqual(prodos.volume_name(image), "WORK")

    def test_duplicate_and_full_volume_are_refused_and_leave_the_volume_unchanged(self):
        image = bytearray(prodos.build_blank_volume("WORK", "140K"))
        prodos.add_file(image, "ONE", b"1", 0x06)
        before = bytes(image)
        with self.assertRaises(DiskImageError):
            prodos.add_file(image, "ONE", b"2", 0x06)
        with self.assertRaises(DiskImageError):
            prodos.add_file(image, "HUGE", b"x" * 200000, 0x06)
        self.assertEqual(bytes(image), before)

    def test_sector_order_conversion_round_trips(self):
        data = b"".join(bytes([index % 251]) * 256 for index in range(560))
        converted = prodos.dos_order_from_prodos_order(data)
        self.assertNotEqual(converted, data)
        self.assertEqual(prodos.prodos_order_from_dos_order(converted), data)

    def test_dos_order_dsk_is_loaded_as_prodos_order(self):
        image = prodos.build_blank_volume("WORK", "140K")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "work.dsk"
            path.write_bytes(prodos.dos_order_from_prodos_order(image))
            self.assertEqual(bytes(prodos.load_volume(path)), image)

    def test_prepare_source_file_maps_names_types_and_startup(self):
        name, file_type, aux, payload = prodos.prepare_source_file("HELLO.BAS", b'10 PRINT "HI"\n')
        self.assertEqual((name, file_type, aux), ("STARTUP", 0xFC, 0x0801))
        self.assertNotEqual(payload, b'10 PRINT "HI"\n')
        self.assertEqual(prodos.prepare_source_file("TEST-NONGR.BAS", b"10 END\n")[0], "TEST.NONGR")
        self.assertEqual(prodos.prepare_source_file("NOTES.TXT", b"a\r\nb")[3], b"a\rb\r")
        self.assertEqual(prodos.prepare_source_file("PROG.SYS", b"x")[1:3], (0xFF, 0x2000))
        with self.assertRaises(DiskImageError):
            prodos.prepare_source_file("RAW.BIN", b"x")
        self.assertEqual(prodos.prepare_source_file("RAW.BIN", b"x", 0x300)[1:3], (0x06, 0x300))

    def test_boot_from_copies_boot_blocks_and_system_files(self):
        base = PROJECT_ROOT / "bootDisks" / "apple2" / "prodosEmpty.po"
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "boot800.po"
            self.assertEqual(prodos.main([str(target), "--name", "BOOT", "--boot-from", str(base)]), 0)
            image = prodos.load_volume(target)
            self.assertEqual(len(image), 819200)
            self.assertEqual([entry["name"] for entry in prodos.list_files(image)], list(prodos.BOOT_FILES))
            self.assertEqual(bytes(image[:1024]), base.read_bytes()[:1024])


if __name__ == "__main__":
    unittest.main()
