#!/usr/bin/env python3
"""Fixtures and cross-checks for tests/guestFsProdosTests.c (native ProDOS guest file system).

  build <dir>      create Python-built ProDOS images (140K/640K/800K ProDOS order, 140K DOS order),
                   the expected images after the C test adds the same files, and manifests
  real <dir>       dump the files of the real ProDOS images for read comparison
  verify <dir>     check images written by guestFsProdosTests with the Python tools
"""

import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools" / "include"))
sys.path.insert(0, str(root / "tools"))
sys.path.insert(0, str(root / "tools" / "apple2"))

import diskImageApple2Prodos as p
from diskImageCommon import DiskImageError

BLOCK_SIZE = 512
DIRECTORY_CAPACITY = 51
HEADER = 2 * BLOCK_SIZE

#-- (stem, profile, extension, sector order)
VARIANTS = [
    ("pr140po", "140K", "po", "prodos"),
    ("pr640po", "640K", "po", "prodos"),
    ("pr800po", "800K", "po", "prodos"),
    ("pr140do", "140K", "do", "dos"),
    ("pr140dsk", "140K", "dsk", "dos"),
]

#-- (name, size, seed, type, aux, access, holes)
COMMON_BASE = [
    ("ONE.TXT", 1, 1, 0x04, 0, 0xC3, ()),
    ("A512.BIN", 512, 2, 0x06, 0x2000, 0xC3, ()),
    ("B513.BAS", 513, 3, 0xFC, 0x0801, 0xC3, ()),
    ("GAP1.DAT", 700, 6, 0x00, 0, 0xC3, ()),
    ("MID.TXT", 3000, 4, 0x04, 0, 0xC3, ()),
    ("GAP2.DAT", 1200, 8, 0xEF, 0, 0xC3, ()),
    ("EXACT", 1536, 5, 0xF0, 0, 0xC3, ()),
    ("KEEP.TXT", 100, 7, 0x04, 0, 0xC3, ()),
    ("SPARSE.BIN", 5000, 9, 0x06, 0x300, 0xC3, (1, 4)),
    ("LOCKED.TXT", 200, 10, 0x04, 0, 0x21, ()),
]
LARGE_BASE = [
    ("TREE.BIN", 270 * 512, 18, 0x06, 0x4000, 0xC3, ()),
    ("SPARSET.BIN", 260 * 512, 19, 0x06, 0x4000, 0xC3, (3, 256, 257, 258, 259)),
]
REMOVED_FILES = ["GAP1.DAT", "GAP2.DAT"]

#-- (name, size, seed, mode, type, aux, access); mode 0 = no attributes (the adapter derives type and aux)
COMMON_ADDED = [
    ("FILLS2.DAT", 1000, 11, 1, 0x06, 0x1234, 0xC3),
    ("BIG.TXT", 1800, 12, 0, None, None, 0xC3),
    ("PROG.BAS", 10, 13, 0, None, None, 0xC3),
    ("NOSUFFIX", 40, 15, 0, None, None, 0xC3),
    ("PROG.SYS", 513, 14, 0, None, None, 0xC3),
    ("LOCKED2.DAT", 513, 16, 1, 0xEF, 0, 0x21),
    ("SAPLING.DAT", 40000, 17, 1, 0xFD, 7, 0xC3),
]
LARGE_ADDED = [
    ("TREEADD.DAT", 140000, 20, 1, 0x06, 0x500, 0xC3),
]

REAL_IMAGES = [
    "sdcard/retro/images/apple2/data1.po",
    "sdcard/retro/images/apple2/data2.po",
    "sdcard/retro/images/apple2/test1.dsk",
    "littlefs/apple2/system.dsk",
    "sdcard/retro/images/ucsd/ApplePascal2-640K.po",
    "sdcard/retro/images/ucsd/pascalWork1.po",
    "littlefs/ucsd/ApplePascal1.3-800K.po",
]


def content(size, seed):
    return bytes(((index * 7 + seed * 13 + (index >> 8)) & 0xFF) for index in range(size))


def derivedType(name):
    dot = name.rfind(".")
    suffix = name[dot:].upper() if dot >= 0 else ""
    return {".BAS": (0xFC, 0x0801), ".TXT": (0x04, 0), ".BIN": (0x06, 0), ".SYS": (0xFF, 0x2000)}.get(suffix, (0, 0))


def u16(image, offset):
    return image[offset] | (image[offset + 1] << 8)


def findSlot(image, name):
    for block, offset in p._directory_entries(image):
        base = block * BLOCK_SIZE + offset
        length = image[base] & 0x0F
        if image[base] >> 4 != 0 and bytes(image[base + 1:base + 1 + length]).decode("ascii") == name:
            return base
    raise DiskImageError(f"no slot for {name}")


def getPointer(image, block, position):
    return image[block * BLOCK_SIZE + position] | (image[block * BLOCK_SIZE + 256 + position] << 8)


def setPointer(image, block, position, value):
    image[block * BLOCK_SIZE + position] = value & 0xFF
    image[block * BLOCK_SIZE + 256 + position] = value >> 8


def releaseBlock(image, number):
    bitmapBlock, _ = p._bitmap_geometry(image)
    image[bitmapBlock * BLOCK_SIZE + number // 8] |= 0x80 >> (number & 7)


def ownedBlocks(image, base):
    storage = image[base] >> 4
    key = u16(image, base + 0x11)
    owned = [key]
    if storage == 2:
        owned += [getPointer(image, key, i) for i in range(256) if getPointer(image, key, i)]
    elif storage == 3:
        for slot in range(128):
            sub = getPointer(image, key, slot)
            if sub:
                owned.append(sub)
                owned += [getPointer(image, sub, i) for i in range(256) if getPointer(image, sub, i)]
    return owned


def punch(image, name, holes):
    """Turn the given data blocks of a file into sparse holes (pointer 0, block released)."""
    base = findSlot(image, name)
    storage = image[base] >> 4
    key = u16(image, base + 0x11)
    freed = 0
    for hole in holes:
        if storage == 2:
            index, position = key, hole
        else:
            index, position = getPointer(image, key, hole // 256), hole % 256
        if index and getPointer(image, index, position):
            releaseBlock(image, getPointer(image, index, position))
            setPointer(image, index, position, 0)
            freed += 1
    if storage == 3:
        for slot in range(128):
            sub = getPointer(image, key, slot)
            if sub and all(getPointer(image, sub, i) == 0 for i in range(256)):
                setPointer(image, key, slot, 0)
                releaseBlock(image, sub)
                freed += 1
    struct.pack_into("<H", image, base + 0x13, u16(image, base + 0x13) - freed)


def addFiles(image, files):
    for name, size, seed, fileType, aux, access, holes in files:
        p.add_file(image, name, content(size, seed), fileType, aux, access)
        if holes:
            punch(image, name, holes)


def addDerived(image, files):
    for name, size, seed, mode, fileType, aux, access in files:
        if mode == 0:
            fileType, aux = derivedType(name)
        p.add_file(image, name, content(size, seed), fileType, aux, access)


def removeFiles(image, names):
    for name in names:
        base = findSlot(image, name)
        for number in ownedBlocks(image, base):
            releaseBlock(image, number)
        image[base] = 0
        struct.pack_into("<H", image, HEADER + 0x25, u16(image, HEADER + 0x25) - 1)


def addSubdirectory(image, name):
    slot = next((block, offset) for block, offset in p._directory_entries(image) if image[block * BLOCK_SIZE + offset] >> 4 == 0)
    key = p._allocate(image, 1)[0]
    image[key * BLOCK_SIZE:(key + 1) * BLOCK_SIZE] = bytes(BLOCK_SIZE)
    image[key * BLOCK_SIZE + 4] = 0xE0 | len(name)
    image[key * BLOCK_SIZE + 5:key * BLOCK_SIZE + 5 + len(name)] = name.encode("ascii")
    base = slot[0] * BLOCK_SIZE + slot[1]
    image[base:base + p.ENTRY_SIZE] = bytes(p.ENTRY_SIZE)
    image[base] = 0xD0 | len(name)
    image[base + 1:base + 1 + len(name)] = name.encode("ascii")
    image[base + 0x10] = 0x0F
    struct.pack_into("<HH", image, base + 0x11, key, 1)
    image[base + 0x15:base + 0x18] = (512).to_bytes(3, "little")
    image[base + 0x1E] = 0xE3
    struct.pack_into("<H", image, base + 0x25, 2)
    struct.pack_into("<H", image, HEADER + 0x25, u16(image, HEADER + 0x25) + 1)


def freeCounts(image):
    return p.free_blocks(image), DIRECTORY_CAPACITY - len(p.list_files(image))


def saveImage(path, image, order):
    data = bytes(image)
    path.write_bytes(p.dos_order_from_prodos_order(data) if order == "dos" else data)


def build(outDir):
    outDir.mkdir(parents=True, exist_ok=True)
    for stem, profile, extension, order in VARIANTS:
        large = profile != "140K"
        baseFiles = COMMON_BASE + (LARGE_BASE if large else [])
        addedFiles = COMMON_ADDED + (LARGE_ADDED if large else [])
        image = bytearray(p.build_blank_volume("TESTVOL", profile))
        addFiles(image, baseFiles)
        addSubdirectory(image, "SUBDIR")
        removeFiles(image, REMOVED_FILES)
        saveImage(outDir / f"{stem}.base.{extension}", image, order)
        freeBlocks, freeSlots = freeCounts(image)
        addDerived(image, addedFiles)
        saveImage(outDir / f"{stem}.expected.{extension}", image, order)
        freeBlocksAfter, freeSlotsAfter = freeCounts(image)
        lines = [f"ext {extension}", f"profile {p.VOLUME_BLOCKS[profile]}"]
        for name, size, seed, fileType, aux, access, holes in baseFiles:
            if name not in REMOVED_FILES:
                holeText = ",".join(str(h) for h in holes) if holes else "-"
                lines.append(f"base {name} {size} {seed} {fileType} {aux} {access} {holeText}")
        lines.append("subdir SUBDIR")
        for name, size, seed, mode, fileType, aux, access in addedFiles:
            expectType, expectAux = (fileType, aux) if mode else derivedType(name)
            lines.append(f"add {name} {size} {seed} {mode} {expectType} {expectAux} {access}")
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
        try:
            image = p.load_volume(path)
        except DiskImageError:
            continue
        dumpDir = outDir / f"real-{index}.files"
        dumpDir.mkdir(exist_ok=True)
        freeBlocks, freeSlots = freeCounts(image)
        lines = [f"image {relative}", f"ext {path.suffix[1:]}", f"free {freeBlocks} {freeSlots}"]
        for entry in p.list_files(image):
            if entry["storage"] in (1, 2, 3):
                data, _ = p.read_file(image, entry["name"])
                (dumpDir / entry["name"]).write_bytes(data)
                lines.append(f"file {entry['name']} {entry['type']} {entry['aux']} {entry['access']} {len(data)}")
            else:
                lines.append(f"other {entry['name']} {entry['storage']}")
        (outDir / f"real-{index}.manifest").write_text("\n".join(lines) + "\n")
        index += 1


def checkBitmap(image, label):
    bitmapBlock, total = p._bitmap_geometry(image)
    bitmapBlocks = (total + 4095) // 4096
    claimed = {0, 1}
    block = 2
    seen = set()
    while block != 0 and block not in seen:
        seen.add(block)
        claimed.add(block)
        block = u16(image, block * BLOCK_SIZE + 2)
    claimed.update(range(bitmapBlock, bitmapBlock + bitmapBlocks))
    for entryBlock, offset in p._directory_entries(image):
        base = entryBlock * BLOCK_SIZE + offset
        storage = image[base] >> 4
        if storage in (1, 2, 3):
            owned = ownedBlocks(image, base)
        elif storage == 0xD:
            owned = [u16(image, base + 0x11)]
        else:
            continue
        for number in owned:
            assert number < total, f"{label}: block {number} outside the volume"
            assert number not in claimed, f"{label}: block {number} is claimed twice"
            claimed.add(number)
    bitmap = image[bitmapBlock * BLOCK_SIZE:]
    for number in claimed:
        assert not bitmap[number // 8] & (0x80 >> (number & 7)), f"{label}: block {number} used but marked free"


def verify(outDir):
    checked = 0
    for filesDir in sorted(outDir.glob("*.files")):
        imagePath = outDir / filesDir.name[:-len(".files")]
        if not imagePath.is_file():
            continue
        image = p.load_volume(imagePath)
        entries = p.list_files(image)
        checkBitmap(image, imagePath.name)
        regular = [e for e in entries if e["storage"] in (1, 2, 3)]
        dumped = sorted(f.name for f in filesDir.iterdir() if f.name != "free.txt")
        assert sorted(e["name"] for e in regular) == dumped, f"{imagePath.name}: file list differs"
        for entry in regular:
            data, _ = p.read_file(image, entry["name"])
            assert data == (filesDir / entry["name"]).read_bytes(), f"{imagePath.name}: {entry['name']} differs"
        freeBlocks, freeSlots = [int(x) for x in (filesDir / "free.txt").read_text().split()]
        assert (freeBlocks, freeSlots) == freeCounts(image), f"{imagePath.name}: free counts differ"
        assert u16(image, HEADER + 0x25) == len(entries), f"{imagePath.name}: header file count is wrong"
        checked += 1
    assert checked > 0, "no images to verify"
    print(f"verified {checked} ProDOS images")


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("build", "real", "verify"):
        print(__doc__)
        return 2
    {"build": build, "real": real, "verify": verify}[sys.argv[1]](Path(sys.argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
