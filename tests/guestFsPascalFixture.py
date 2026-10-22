#!/usr/bin/env python3
"""Fixtures and cross-checks for tests/guestFsPascalTests.c (native UCSD Pascal guest file system).

  build <dir>      create Python-built Pascal images (140K/640K/800K, ProDOS and DOS sector order),
                   the expected images after the C test adds the same files, and manifests
  real <dir>       dump the files of the real UCSD Pascal images for read comparison
  verify <dir>     check images written by guestFsPascalTests with the Python tools
"""

import sys
import types
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools" / "include"))
sys.path.insert(0, str(root / "tools"))

import extractApple2Files as tool
import diskImageApple2Pascal

BLOCK_SIZE = 512
KIND_BY_SUFFIX = {".TEXT": 3, ".CODE": 2, ".DATA": 5, ".INFO": 4, ".GRAF": 6, ".FOTO": 7}

#-- (stem, blocks, extension, sector order)
VARIANTS = [
    ("p140po", 280, "po", "prodos"),
    ("p640po", 1280, "po", "prodos"),
    ("p800po", 1600, "po", "prodos"),
    ("p140do", 280, "do", "dos"),
    ("p140dsk", 280, "dsk", "dos"),
]

#-- (name, size, seed, kind); kind 0 = derive from the name like the C adapter does
BASE_FILES = [
    ("ONE.TEXT", 1, 1, 3),
    ("A512.CODE", 512, 2, 2),
    ("B513.DATA", 513, 3, 5),
    ("GAP1.DATA", 700, 6, 5),
    ("MID.INFO", 3000, 4, 4),
    ("GAP2.DATA", 1200, 8, 5),
    ("EXACT", 1536, 5, 5),
    ("KEEP.TEXT", 100, 7, 3),
]
REMOVED_FILES = ["GAP1.DATA", "GAP2.DATA"]
ADDED_FILES = [
    ("FILLS2.DATA", 1000, 11, 5),
    ("BIG.TEXT", 1800, 12, 3),
    ("SMALL.GRAF", 10, 13, 6),
    ("DERIVED.CODE", 600, 14, 0),
    ("NOSUFFIX", 40, 15, 0),
    ("PHOTO.FOTO", 513, 16, 0),
]

REAL_IMAGES = [
    "sdcard/retro/images/ucsd/ApplePascal2-640K.po",
    "sdcard/retro/images/ucsd/ApplePascal2-640K-BACKUP.po",
    "sdcard/retro/images/ucsd/pascalWork1.po",
    "sdcard/retro/images/ucsd/pascalWork2.po",
    "littlefs/ucsd/ApplePascal1.3-800K.po",
]


class FakeDateTime:
    month = 0
    day = 0
    year = 2000

    @classmethod
    def now(cls):
        return cls()


#-- The C adapter stores date 0; make the Python reference do the same so images compare byte for byte.
tool.datetime = types.SimpleNamespace(datetime=FakeDateTime)


def content(size, seed):
    return bytes(((index * 7 + seed * 13 + (index >> 8)) & 0xFF) for index in range(size))


def derivedKind(name):
    dot = name.rfind(".")
    return KIND_BY_SUFFIX.get(name[dot:].upper(), 5) if dot >= 0 else 5


def blankPascal(blocks):
    if blocks == 280:
        return diskImageApple2Pascal.build_blank_volume("TESTVOL", "140K")
    if blocks == 1280:
        return diskImageApple2Pascal.build_blank_volume("TESTVOL", "640K")
    image = bytearray(blocks * BLOCK_SIZE)
    header = 2 * BLOCK_SIZE
    image[header + 2] = 6
    image[header + 6] = 7
    image[header + 7:header + 14] = b"TESTVOL"
    image[header + 14] = blocks & 0xFF
    image[header + 15] = blocks >> 8
    return bytes(image)


def writeImage(path, blocks, order):
    blank = blankPascal(blocks)
    path.write_bytes(blank)
    if order == "dos":
        source = tool.DiskImage(path)
        source.order = "prodos"
        path.write_bytes(bytes(len(blank)))
        converted = tool.DiskImage(path)
        converted.order = "dos"
        for block in range(blocks):
            converted.writeBlock(block, source.readBlock(block))
        converted.save()


def openPascal(path, order):
    image = tool.DiskImage(path)
    image.order = order
    return image, tool.PascalVolume(image)


def addFiles(path, order, files):
    image, volume = openPascal(path, order)
    for name, size, seed, kind in files:
        data = content(size, seed)
        volume.importFile(name, tool.Exported(data, 0, 0, kind if kind else derivedKind(name)))
    image.save()


def removeFiles(path, order, names):
    image, volume = openPascal(path, order)
    for name in names:
        entry = [e for e in volume.listFiles() if e.path == name][0]
        volume.removeFile(entry)
    image.save()


def freeCounts(path, order):
    image, volume = openPascal(path, order)
    raw, entries = volume.readDirectory()
    capacity = len(raw) // 26 - 1
    used = sum(e["next"] - e["first"] for e in entries)
    return volume.totalBlocks - volume.dirEnd - used, capacity - len(entries)


def build(outDir):
    outDir.mkdir(parents=True, exist_ok=True)
    for stem, blocks, extension, order in VARIANTS:
        base = outDir / f"{stem}.base.{extension}"
        expected = outDir / f"{stem}.expected.{extension}"
        writeImage(base, blocks, order)
        addFiles(base, order, BASE_FILES)
        removeFiles(base, order, REMOVED_FILES)
        expected.write_bytes(base.read_bytes())
        addFiles(expected, order, ADDED_FILES)
        freeBlocks, freeSlots = freeCounts(base, order)
        freeBlocksAfter, freeSlotsAfter = freeCounts(expected, order)
        lines = [f"ext {extension}", f"profile {blocks}", f"total {blocks}"]
        for name, size, seed, kind in BASE_FILES:
            if name not in REMOVED_FILES:
                lines.append(f"base {name} {size} {seed} {kind if kind else derivedKind(name)}")
        for name, size, seed, kind in ADDED_FILES:
            lines.append(f"add {name} {size} {seed} {kind}")
        lines.append(f"free {freeBlocks} {freeSlots}")
        lines.append(f"freeAfter {freeBlocksAfter} {freeSlotsAfter}")
        (outDir / f"{stem}.manifest").write_text("\n".join(lines) + "\n")


def real(outDir):
    outDir.mkdir(parents=True, exist_ok=True)
    index = 0
    for relative in REAL_IMAGES:
        path = root / relative
        if not path.is_file():
            continue
        image = tool.DiskImage(path)
        image.order = "prodos"
        volume = tool.PascalVolume(image)
        dumpDir = outDir / f"real-{index}.files"
        dumpDir.mkdir(exist_ok=True)
        raw, entries = volume.readDirectory()
        capacity = len(raw) // 26 - 1
        used = sum(e["next"] - e["first"] for e in entries)
        lines = [f"image {relative}", f"free {volume.totalBlocks - volume.dirEnd - used} {capacity - len(entries)}"]
        for entry in volume.listFiles():
            exported = volume.exportFile(entry)
            (dumpDir / entry.path).write_bytes(exported.data)
            lines.append(f"file {entry.path} {entry.meta['kind']} {len(exported.data)}")
        (outDir / f"real-{index}.manifest").write_text("\n".join(lines) + "\n")
        index += 1


def checkLayout(image, volume, label):
    raw, entries = volume.readDirectory()
    capacity = len(raw) // 26 - 1
    assert len(entries) <= capacity, f"{label}: too many entries"
    ordered = sorted(entries, key=lambda e: e["first"])
    cursor = volume.dirEnd
    for entry in ordered:
        assert entry["first"] >= cursor, f"{label}: {entry['name']} overlaps or starts inside the directory"
        assert entry["next"] > entry["first"], f"{label}: {entry['name']} is empty"
        assert 1 <= entry["lastBytes"] <= BLOCK_SIZE, f"{label}: bad lastBytes for {entry['name']}"
        cursor = entry["next"]
    assert cursor <= volume.totalBlocks, f"{label}: file beyond the volume"
    assert [e["first"] for e in entries] == [e["first"] for e in ordered], f"{label}: directory not sorted"


def verify(outDir):
    checked = 0
    for filesDir in sorted(outDir.glob("*.files")):
        imagePath = outDir / filesDir.name[:-len(".files")]
        if not imagePath.is_file():
            continue
        image = tool.DiskImage(imagePath)
        volume = tool.openVolume(image)
        assert isinstance(volume, tool.PascalVolume), f"{imagePath.name}: not recognised as Pascal"
        checkLayout(image, volume, imagePath.name)
        entries = volume.listFiles()
        dumped = sorted(p.name for p in filesDir.iterdir() if p.name != "free.txt")
        assert sorted(e.path for e in entries) == dumped, f"{imagePath.name}: file list differs"
        for entry in entries:
            exported = volume.exportFile(entry)
            assert exported.data == (filesDir / entry.path).read_bytes(), f"{imagePath.name}: {entry.path} differs"
        freeBlocks, freeSlots = [int(x) for x in (filesDir / "free.txt").read_text().split()]
        raw, rawEntries = volume.readDirectory()
        capacity = len(raw) // 26 - 1
        used = sum(e["next"] - e["first"] for e in rawEntries)
        assert freeBlocks == volume.totalBlocks - volume.dirEnd - used, f"{imagePath.name}: free blocks differ"
        assert freeSlots == capacity - len(rawEntries), f"{imagePath.name}: free slots differ"
        checked += 1
    assert checked > 0, "no images to verify"
    print(f"verified {checked} Pascal images")


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("build", "real", "verify"):
        print(__doc__)
        return 2
    {"build": build, "real": real, "verify": verify}[sys.argv[1]](Path(sys.argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
