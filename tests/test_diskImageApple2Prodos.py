import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import diskImageApple2Prodos as prodos
from diskImageCommon import DiskImageError


class BlankProdosVolumeTests(unittest.TestCase):
    def test_640k_geometry_and_header(self):
        image = prodos.build_blank_volume("data640")
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
        image = prodos.build_blank_volume("WORK")
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
            prodos.build_blank_volume("WORK", "800K")

    def test_command_line_writes_file(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "blank.po"
            self.assertEqual(prodos.main([str(target), "--name", "DATA", "--profile", "140K"]), 0)
            self.assertEqual(target.stat().st_size, 143360)
            self.assertEqual(prodos.main([str(target), "--name", "BAD NAME"]), 1)


if __name__ == "__main__":
    unittest.main()
