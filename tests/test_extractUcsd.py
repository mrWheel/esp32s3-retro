import contextlib
import io
import sys
import tempfile
import unittest
import zlib
from types import SimpleNamespace
from pathlib import Path
from unittest.mock import patch

repositoryRoot = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(repositoryRoot / "tools" / "include"))
sys.path.insert(0, str(repositoryRoot / "tools"))

import diskImageApple2Pascal
import extractFiles
import extractApple2Files
import extractUcsd

PRODOS_SECTOR_OF_PHYSICAL = [0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15]


def makeWozImage(prodosImage, version, writeProtected=False):
    tracks = []
    for track in range(35):
        trackData = bytearray()
        for physicalSector in range(16):
            trackData.extend((0xD5, 0xAA, 0x96))
            volume = 254
            for value in (volume, track, physicalSector, volume ^ track ^ physicalSector):
                trackData.extend(((value >> 1) | 0xAA, value | 0xAA))
            trackData.extend((0xDE, 0xAA, 0xEB))
            trackData.extend(bytes([0xFF]) * 8)
            trackData.extend((0xD5, 0xAA, 0xAD))
            sector = PRODOS_SECTOR_OF_PHYSICAL[physicalSector]
            start = (track * 16 + sector) * 256
            trackData.extend(extractApple2Files.encodeWozSector(prodosImage[start:start + 256]))
            trackData.extend((0xDE, 0xAA, 0xEB))
            trackData.extend(bytes([0xFF]) * 8)
        tracks.append(bytes(trackData))

    image = bytearray(b"WOZ" + str(version).encode("ascii") + b"\xFF\x0A\x0D\x0A" + bytes(4))

    def addChunk(tag, data):
        image.extend(tag + len(data).to_bytes(4, "little") + data)

    addChunk(b"INFO", bytes([version, 1, int(writeProtected)]) + bytes(57))
    trackMap = bytearray([0xFF] * 160)
    for track in range(35):
        trackMap[track * 4:track * 4 + 4] = bytes([track] * 4)
    addChunk(b"TMAP", trackMap)

    if version == 1:
        trackChunk = bytearray()
        for data in tracks:
            trackChunk.extend(data + bytes(6646 - len(data)))
            trackChunk.extend(len(data).to_bytes(2, "little"))
            trackChunk.extend((len(data) * 8).to_bytes(2, "little"))
            trackChunk.extend(b"\xFF\xFF\x00\x00\x00\x00")
    else:
        trackDataStart = ((len(image) + 8 + 1280 + 511) // 512) * 512
        trackBlock = trackDataStart // 512
        trackChunk = bytearray(1280 + trackDataStart - (len(image) + 8 + 1280))
        for index, data in enumerate(tracks):
            blockCount = (len(data) + 511) // 512
            descriptor = index * 8
            trackChunk[descriptor:descriptor + 2] = trackBlock.to_bytes(2, "little")
            trackChunk[descriptor + 2:descriptor + 4] = blockCount.to_bytes(2, "little")
            trackChunk[descriptor + 4:descriptor + 8] = (len(data) * 8).to_bytes(4, "little")
            trackChunk.extend(data + bytes(blockCount * 512 - len(data)))
            trackBlock += blockCount
    addChunk(b"TRKS", trackChunk)
    image[8:12] = (zlib.crc32(image[12:]) & 0xFFFFFFFF).to_bytes(4, "little")
    return image


def makeTwoImgImage(prodosImage, writeProtected=False, imageFormat=1):
    if imageFormat == 0:
        dosImage = bytearray(len(prodosImage))
        for track in range(35):
            for prodosSector, dosSector in enumerate(extractApple2Files.PRODOS_TO_DOS_SECTOR):
                source = (track * 16 + prodosSector) * 256
                target = (track * 16 + dosSector) * 256
                dosImage[target:target + 256] = prodosImage[source:source + 256]
        prodosImage = bytes(dosImage)
    dataOffset = 80
    comment = b"note"
    image = bytearray(dataOffset)
    image[:4] = b"2IMG"
    image[4:8] = b"TEST"
    image[8:10] = (64).to_bytes(2, "little")
    image[10:12] = (1).to_bytes(2, "little")
    image[12:16] = imageFormat.to_bytes(4, "little")
    image[16:20] = (0x80000000 if writeProtected else 0).to_bytes(4, "little")
    image[20:24] = (len(prodosImage) // 512).to_bytes(4, "little")
    image[24:28] = dataOffset.to_bytes(4, "little")
    image[28:32] = len(prodosImage).to_bytes(4, "little")
    image[32:36] = (64).to_bytes(4, "little")
    image[36:40] = len(comment).to_bytes(4, "little")
    image[40:44] = (dataOffset + len(prodosImage)).to_bytes(4, "little")
    image[44:48] = (4).to_bytes(4, "little")
    image[64:68] = comment
    image.extend(prodosImage)
    image.extend(b"meta")
    return image


class ExtractUcsdImageTests(unittest.TestCase):
    def test_interactive_mode_uses_ucsd_path_defaults(self):
        arguments = SimpleNamespace(image=None, add=None, remove=None, extract=None)
        with patch.object(extractUcsd.core, "main", return_value=0) as menu:
            self.assertEqual(extractUcsd.run(arguments), 0)

        menu.assert_called_once_with(["extractFiles"], ucsdMode=True)

    def test_list_add_extract_and_remove_woz_images(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            for version in (1, 2):
                with self.subTest(wozVersion=version):
                    directory = Path(temporaryDirectory) / f"version{version}"
                    directory.mkdir()
                    imagePath = directory / "work.woz"
                    hostPath = directory / "HELLO.TEXT"
                    extractedPath = directory / "extracted" / "HELLO.TEXT"
                    extractedPath.parent.mkdir()
                    imagePath.write_bytes(makeWozImage(
                        diskImageApple2Pascal.build_blank_volume("WORK", "140K"), version,
                    ))
                    hostData = b"PROGRAM HELLO;\nBEGIN\nEND.\n"
                    hostPath.write_bytes(hostData)

                    with contextlib.redirect_stdout(io.StringIO()) as output:
                        self.assertEqual(extractFiles.main(["--os", "ucsd", str(imagePath)]), 0)
                    self.assertIn("Pascal volume WORK", output.getvalue())

                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(extractFiles.main([
                            "--os", "ucsd", str(imagePath), "--add", str(hostPath), "--raw",
                        ]), 0)
                        self.assertEqual(extractFiles.main([
                            "--os", "ucsd", str(imagePath), "--extract", "HELLO.TEXT",
                            "--dest", str(extractedPath.parent), "--raw",
                        ]), 0)
                    self.assertEqual(extractedPath.read_bytes(), hostData)

                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(extractFiles.main([
                            "--os", "ucsd", str(imagePath), "--remove", "HELLO.TEXT",
                        ]), 0)

                    image = extractApple2Files.DiskImage(imagePath)
                    volume = extractApple2Files.openVolume(image)
                    self.assertEqual(volume.listFiles(), [])

    def test_write_protected_woz_rejects_modification(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "protected.woz"
            imagePath.write_bytes(makeWozImage(
                diskImageApple2Pascal.build_blank_volume("WORK", "140K"), 2, writeProtected=True,
            ))
            original = imagePath.read_bytes()
            image = extractApple2Files.DiskImage(imagePath)
            volume = extractApple2Files.openVolume(image)
            volume.importFile("TEST.TEXT", extractApple2Files.Exported(b"test", 0x0C, 0, 3))
            with self.assertRaisesRegex(extractApple2Files.ToolError, "write-protected"):
                image.save()
            self.assertEqual(imagePath.read_bytes(), original)

    def test_list_add_extract_and_remove_2img_images(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            for imageFormat in (0, 1):
                with self.subTest(imageFormat=imageFormat):
                    directory = Path(temporaryDirectory) / f"format{imageFormat}"
                    directory.mkdir()
                    imagePath = directory / "work.2img"
                    hostPath = directory / "HELLO.TEXT"
                    extractedDirectory = directory / "extracted"
                    extractedDirectory.mkdir()
                    rawImage = diskImageApple2Pascal.build_blank_volume("WORK", "140K")
                    wrapped = makeTwoImgImage(rawImage, imageFormat=imageFormat)
                    imagePath.write_bytes(wrapped)
                    hostData = b"PROGRAM HELLO;\nBEGIN\nEND.\n"
                    hostPath.write_bytes(hostData)

                    with contextlib.redirect_stdout(io.StringIO()) as output:
                        self.assertEqual(extractFiles.main(["--os", "ucsd", str(imagePath)]), 0)
                    self.assertIn("Pascal volume WORK", output.getvalue())

                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(extractFiles.main([
                            "--os", "ucsd", str(imagePath), "--add", str(hostPath), "--raw",
                        ]), 0)
                        self.assertEqual(extractFiles.main([
                            "--os", "ucsd", str(imagePath), "--extract", "HELLO.TEXT",
                            "--dest", str(extractedDirectory), "--raw",
                        ]), 0)
                    self.assertEqual((extractedDirectory / "HELLO.TEXT").read_bytes(), hostData)

                    changed = imagePath.read_bytes()
                    dataOffset = int.from_bytes(changed[24:28], "little")
                    dataLength = int.from_bytes(changed[28:32], "little")
                    self.assertEqual(changed[:dataOffset], wrapped[:dataOffset])
                    self.assertEqual(changed[dataOffset + dataLength:], wrapped[dataOffset + dataLength:])

                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(extractFiles.main([
                            "--os", "ucsd", str(imagePath), "--remove", "HELLO.TEXT",
                        ]), 0)
                    image = extractApple2Files.DiskImage(imagePath)
                    volume = extractApple2Files.openVolume(image)
                    self.assertEqual(volume.listFiles(), [])

    def test_write_protected_2img_rejects_modification(self):
        with tempfile.TemporaryDirectory() as temporaryDirectory:
            imagePath = Path(temporaryDirectory) / "protected.2img"
            imagePath.write_bytes(makeTwoImgImage(
                diskImageApple2Pascal.build_blank_volume("WORK", "140K"), writeProtected=True,
            ))
            original = imagePath.read_bytes()
            image = extractApple2Files.DiskImage(imagePath)
            volume = extractApple2Files.openVolume(image)
            volume.importFile("TEST.TEXT", extractApple2Files.Exported(b"test", 0x0C, 0, 3))
            with self.assertRaisesRegex(extractApple2Files.ToolError, "write-protected"):
                image.save()
            self.assertEqual(imagePath.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
