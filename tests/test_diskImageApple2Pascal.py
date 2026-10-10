import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import diskImageApple2Pascal as pascal
from diskImageCommon import DiskImageError


class BlankPascalVolumeTests(unittest.TestCase):
    def test_640k_geometry_and_header(self):
        image = pascal.build_blank_volume("work")
        self.assertEqual(len(image), 655360)
        self.assertEqual(len(image), 160 * 16 * 256)
        header = image[1024:1024 + 26]
        self.assertEqual(struct.unpack_from("<HHH", header, 0), (0, 6, 0))
        self.assertEqual(header[6], 4)
        self.assertEqual(header[7:11], b"WORK")
        self.assertEqual(struct.unpack_from("<HH", header, 0x0E), (1280, 0))

    def test_140k_volume(self):
        image = pascal.build_blank_volume("SMALL", "140K")
        self.assertEqual(len(image), 143360)
        self.assertEqual(struct.unpack_from("<H", image, 1024 + 0x0E)[0], 280)

    def test_everything_else_is_zero(self):
        image = bytearray(pascal.build_blank_volume("X"))
        image[1024:1024 + 26] = bytes(26)
        self.assertEqual(image.count(0), len(image))

    def test_name_rules(self):
        for bad in ("", "TOOLONGNAME", "A B", "A=B", "A?", "A,B"):
            with self.assertRaises(DiskImageError):
                pascal.build_blank_volume(bad)

    def test_unknown_profile(self):
        with self.assertRaises(DiskImageError):
            pascal.build_blank_volume("OK", "800K")

    def test_command_line_writes_file(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "blank.po"
            self.assertEqual(pascal.main([str(target), "--name", "DATA", "--profile", "140K"]), 0)
            self.assertEqual(target.stat().st_size, 143360)
            self.assertEqual(pascal.main([str(target), "--name", "WAYTOOLONG"]), 1)


if __name__ == "__main__":
    unittest.main()
