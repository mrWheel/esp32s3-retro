#!/usr/bin/env python3
"""Fixtures and cross-checks for tests/guestFsDos33Tests.c (native DOS 3.3 guest file system).

  build <dir>      create Python-built DOS 3.3 images (.do/.dsk DOS order, .po ProDOS order), the expected
                   images after the C test adds the same files, and manifests
  real <dir>       dump the files of real DOS 3.3 images (read with tools/extractApple2Files.py)
  verify <dir>     strictly check images written by guestFsDos33Tests (structure, bitmap, file contents)

The writer below is an independent implementation of the DOS 3.3 layout described at the top of
components/guestFs/guestFsDos33.c, including its allocation order (tracks 1..34 without 17, sectors
15..0, then track 0), so that C and Python must produce byte identical images.
"""

import struct
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools"))

import extractApple2Files as x

TRACKS = 35
SECTORS = 16
SECTOR_SIZE = 256
IMAGE_SIZE = TRACKS * SECTORS * SECTOR_SIZE
CATALOG_TRACK = 17
ENTRY_OFFSET = 0x0B
ENTRY_SIZE = 35
PAIRS = 122
PRODOS_TO_DOS = [0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15]
assert PRODOS_TO_DOS == x.PRODOS_TO_DOS_SECTOR

HEADER_BYTES = {0x01: 2, 0x02: 2, 0x04: 4}

#-- (stem, extension, sector order, track 0 free)
VARIANTS = [
    ("dos140do", "do", "dos", False),
    ("dos140dsk", "dsk", "dos", False),
    ("dos140po", "po", "prodos", False),
    ("dos140t0", "dsk", "dos", True),
]

#-- (name, size, seed, rawType, address, locked, holes); holes are raw sector numbers (header included)
COMMON_BASE = [
    ("HELLO", 1, 1, 0x00, 0, False, ()),
    ("TEXT256", 256, 2, 0x00, 0, False, ()),
    ("GAP1.DAT", 700, 6, 0x00, 0, False, ()),
    ("INTFILE", 600, 3, 0x01, 0, False, ()),
    ("BASPROG", 513, 4, 0x02, 0, False, ()),
    ("MID.TXT", 3000, 5, 0x00, 0, False, ()),
    ("LOADER", 1000, 7, 0x04, 0x0300, False, ()),
    ("GAP2.DAT", 1200, 8, 0x04, 0x2000, False, ()),
    ("REL.OBJ", 768, 9, 0x10, 0, False, ()),
    ("DATA.S", 512, 10, 0x08, 0, False, ()),
    ("KEEP.TXT", 100, 12, 0x00, 0, False, ()),
    ("LOCKED.TXT", 200, 13, 0x00, 0, True, ()),
    ("SPARSE.TXT", 5000, 14, 0x00, 0, False, (1, 4)),
    ("SPARSE.BIN", 5000, 15, 0x04, 0x2000, False, (2, 5)),
    ("TREE.BIN", 40000, 16, 0x04, 0x4000, False, ()),
]
REMOVED_FILES = ["GAP1.DAT", "GAP2.DAT"]

#-- (name, size, seed, mode, prodosType, aux, locked); mode 0 = no attributes passed to the adapter
COMMON_ADDED = [
    ("FILLS.BIN", 1000, 21, 1, 0x06, 0x1234, False),
    ("BIG.TXT", 1800, 22, 0, 0, 0, False),
    ("PROG.BAS", 10, 23, 1, 0xFC, 0x0801, False),
    ("INTPROG", 300, 24, 1, 0xFA, 0x0800, False),
    ("RELOC", 512, 25, 1, 0xFE, 0, False),
    ("LOCKED2.BIN", 513, 26, 1, 0x06, 0x4000, True),
    ("OTHER.TYPE", 256, 27, 1, 0xEF, 5, False),
    ("BIG122", 31228, 28, 1, 0x06, 0x6000, False),
]

DOS_TYPE_OF_PRODOS = {0x04: 0x00, 0xFA: 0x01, 0xFC: 0x02, 0xFE: 0x10, 0x06: 0x04}


def content(size, seed):
    return bytes(((index * 7 + seed * 13 + (index >> 8)) & 0xFF) for index in range(size))


def readSizeOf(rawType, size):
    if rawType in (0x00, 0x01, 0x02, 0x04):
        return size
    return (size + 255) // 256 * 256


def addedType(mode, prodosType, aux):
    """DOS 3.3 file type and load address the adapter must choose for the given attributes."""
    if mode == 0:
        return 0x00, 0
    if prodosType in DOS_TYPE_OF_PRODOS:
        rawType = DOS_TYPE_OF_PRODOS[prodosType]
        return rawType, (aux if rawType == 0x04 else 0)
    return 0x04, 0


def decodeName(entry):
    text = bytes(b & 0x7F for b in entry[3:33]).decode("ascii", "replace")
    return text.rstrip()


class Volume:
    def __init__(self, track0Free):
        self.data = bytearray(IMAGE_SIZE)
        vtoc = self.sector(CATALOG_TRACK, 0)
        vtoc[0] = 4
        vtoc[1] = CATALOG_TRACK
        vtoc[2] = 15
        vtoc[3] = 3
        vtoc[6] = 254
        vtoc[0x27] = PAIRS
        vtoc[0x30] = CATALOG_TRACK
        vtoc[0x31] = 1
        vtoc[0x34] = TRACKS
        vtoc[0x35] = SECTORS
        vtoc[0x36] = 0
        vtoc[0x37] = 1
        self.writeSector(CATALOG_TRACK, 0, vtoc)
        for track in range(TRACKS):
            if track == CATALOG_TRACK or (track == 0 and not track0Free):
                continue
            for sector in range(SECTORS):
                self.setFree(track, sector, True)
        for sector in range(15, 0, -1):
            catalog = bytearray(SECTOR_SIZE)
            if sector > 1:
                catalog[1] = CATALOG_TRACK
                catalog[2] = sector - 1
            self.writeSector(CATALOG_TRACK, sector, catalog)

    def offset(self, track, sector):
        return (track * SECTORS + sector) * SECTOR_SIZE

    def sector(self, track, sector):
        offset = self.offset(track, sector)
        return bytearray(self.data[offset:offset + SECTOR_SIZE])

    def writeSector(self, track, sector, buffer):
        assert len(buffer) == SECTOR_SIZE
        offset = self.offset(track, sector)
        self.data[offset:offset + SECTOR_SIZE] = buffer

    def bitmapPosition(self, track, sector):
        return self.offset(CATALOG_TRACK, 0) + 0x38 + 4 * track + (0 if sector >= 8 else 1), 1 << (sector & 7)

    def isFree(self, track, sector):
        position, mask = self.bitmapPosition(track, sector)
        return bool(self.data[position] & mask)

    def setFree(self, track, sector, free):
        position, mask = self.bitmapPosition(track, sector)
        if free:
            self.data[position] |= mask
        else:
            self.data[position] &= ~mask & 0xFF

    def freeSectors(self):
        return sum(self.isFree(t, s) for t in range(TRACKS) for s in range(SECTORS) if (t, s) != (0, 0))

    def allocate(self, count):
        found = []
        for track in list(range(1, TRACKS)) + [0]:
            if track == CATALOG_TRACK:
                continue
            for sector in range(SECTORS - 1, -1, -1):
                if len(found) < count and (track, sector) != (0, 0) and self.isFree(track, sector):
                    found.append((track, sector))
        assert len(found) == count, "not enough free sectors"
        return found

    def slots(self):
        track, sector = CATALOG_TRACK, 15
        result = []
        while track != 0:
            buffer = self.sector(track, sector)
            for slot in range(7):
                result.append((track, sector, ENTRY_OFFSET + ENTRY_SIZE * slot))
            track, sector = buffer[1], buffer[2]
        return result

    def entry(self, slotInfo):
        track, sector, offset = slotInfo
        return self.sector(track, sector)[offset:offset + ENTRY_SIZE]

    def setEntry(self, slotInfo, entry):
        track, sector, offset = slotInfo
        buffer = self.sector(track, sector)
        buffer[offset:offset + ENTRY_SIZE] = entry
        self.writeSector(track, sector, buffer)

    def find(self, name):
        for slotInfo in self.slots():
            entry = self.entry(slotInfo)
            if entry[0] not in (0, 0xFF) and decodeName(entry) == name:
                return slotInfo
        return None

    def freeSlot(self):
        for slotInfo in self.slots():
            if self.entry(slotInfo)[0] in (0, 0xFF):
                return slotInfo
        raise AssertionError("catalog full")

    def freeSlotCount(self):
        return sum(1 for slotInfo in self.slots() if self.entry(slotInfo)[0] in (0, 0xFF))

    def fileSectors(self, entry):
        lists = []
        pairs = []
        track, sector = entry[0], entry[1]
        while track != 0:
            lists.append((track, sector))
            buffer = self.sector(track, sector)
            for pair in range(PAIRS):
                pairs.append((buffer[0x0C + 2 * pair], buffer[0x0D + 2 * pair]))
            track, sector = buffer[1], buffer[2]
        return lists, pairs

    def release(self, entry):
        lists, pairs = self.fileSectors(entry)
        for track, sector in lists:
            self.setFree(track, sector, True)
        for pair in pairs:
            if pair != (0, 0):
                self.setFree(pair[0], pair[1], True)

    def addFile(self, name, body, rawType, address, locked, overwrite=False):
        name = name.upper()
        if rawType in HEADER_BYTES and HEADER_BYTES[rawType] == 2:
            payload = struct.pack("<H", len(body)) + body
        elif rawType == 0x04:
            payload = struct.pack("<HH", address, len(body)) + body
        else:
            payload = body
        dataSectors = (len(payload) + 255) // 256
        listCount = (dataSectors + PAIRS - 1) // PAIRS
        allocation = self.allocate(dataSectors + listCount)
        slotInfo = self.find(name)
        if slotInfo is not None:
            assert overwrite, "file exists"
            self.release(self.entry(slotInfo))
        else:
            slotInfo = self.freeSlot()
        for listNumber in range(listCount):
            base = listNumber * (PAIRS + 1)
            buffer = bytearray(SECTOR_SIZE)
            if listNumber + 1 < listCount:
                buffer[1], buffer[2] = allocation[base + PAIRS + 1]
            struct.pack_into("<H", buffer, 5, listNumber * PAIRS)
            for pair in range(PAIRS):
                dataIndex = listNumber * PAIRS + pair
                if dataIndex >= dataSectors:
                    break
                buffer[0x0C + 2 * pair], buffer[0x0D + 2 * pair] = allocation[base + 1 + pair]
            self.writeSector(*allocation[base], buffer)
        for dataIndex in range(dataSectors):
            chunk = payload[dataIndex * 256:(dataIndex + 1) * 256]
            track, sector = allocation[(dataIndex // PAIRS) * (PAIRS + 1) + 1 + dataIndex % PAIRS]
            self.writeSector(track, sector, bytearray(chunk.ljust(SECTOR_SIZE, b"\0")))
        for track, sector in allocation:
            self.setFree(track, sector, False)
        entry = bytearray(ENTRY_SIZE)
        entry[0], entry[1] = allocation[0]
        entry[2] = rawType | (0x80 if locked else 0)
        entry[3:33] = bytes(ord(c) | 0x80 for c in name).ljust(30, b"\xA0")
        struct.pack_into("<H", entry, 33, len(allocation))
        self.setEntry(slotInfo, entry)

    def punch(self, name, holes):
        slotInfo = self.find(name)
        entry = self.entry(slotInfo)
        lists, pairs = self.fileSectors(entry)
        freed = 0
        for hole in holes:
            track, sector = pairs[hole]
            assert (track, sector) != (0, 0)
            self.setFree(track, sector, True)
            listTrack, listSector = lists[hole // PAIRS]
            buffer = self.sector(listTrack, listSector)
            buffer[0x0C + 2 * (hole % PAIRS)] = 0
            buffer[0x0D + 2 * (hole % PAIRS)] = 0
            self.writeSector(listTrack, listSector, buffer)
            freed += 1
        count = struct.unpack_from("<H", entry, 33)[0] - freed
        struct.pack_into("<H", entry, 33, count)
        self.setEntry(slotInfo, entry)

    def remove(self, name):
        slotInfo = self.find(name)
        entry = self.entry(slotInfo)
        self.release(entry)
        entry[0x20] = entry[0]
        entry[0] = 0xFF
        self.setEntry(slotInfo, entry)


def saveImage(path, volume, order):
    if order == "dos":
        path.write_bytes(bytes(volume.data))
        return
    out = bytearray(IMAGE_SIZE)
    for track in range(TRACKS):
        for sector in range(SECTORS):
            source = volume.offset(track, sector)
            target = (track * SECTORS + PRODOS_TO_DOS[sector]) * SECTOR_SIZE
            out[target:target + SECTOR_SIZE] = volume.data[source:source + SECTOR_SIZE]
    path.write_bytes(bytes(out))


def checkTextEnd(rawType, size, seed):
    if rawType == 0x00:
        assert content(size, seed)[-1] != 0, f"text file of {size} bytes seed {seed} ends with a zero byte"


def build(outDir):
    outDir.mkdir(parents=True, exist_ok=True)
    for stem, extension, order, track0Free in VARIANTS:
        volume = Volume(track0Free)
        for name, size, seed, rawType, address, locked, holes in COMMON_BASE:
            checkTextEnd(rawType, size, seed)
            volume.addFile(name, content(size, seed), rawType, address, locked)
            if holes:
                volume.punch(name, holes)
        for name in REMOVED_FILES:
            volume.remove(name)
        saveImage(outDir / f"{stem}.base.{extension}", volume, order)
        freeSectors, freeSlots = volume.freeSectors(), volume.freeSlotCount()
        added = []
        for name, size, seed, mode, prodosType, aux, locked in COMMON_ADDED:
            rawType, address = addedType(mode, prodosType, aux)
            checkTextEnd(rawType, size, seed)
            volume.addFile(name, content(size, seed), rawType, address, locked)
            added.append((name, size, seed, mode, prodosType, aux, locked, rawType, address))
        saveImage(outDir / f"{stem}.expected.{extension}", volume, order)
        lines = [f"ext {extension}"]
        for name, size, seed, rawType, address, locked, holes in COMMON_BASE:
            if name not in REMOVED_FILES:
                holeText = ",".join(str(h) for h in holes) if holes else "-"
                lines.append(f"base {name} {size} {seed} {rawType} {address} {int(locked)} {readSizeOf(rawType, size)} {holeText}")
        for name, size, seed, mode, prodosType, aux, locked, rawType, address in added:
            lines.append(f"add {name} {size} {seed} {mode} {prodosType} {aux} {int(locked)} {rawType} {address} {readSizeOf(rawType, size)}")
        lines.append(f"free {freeSectors} {freeSlots}")
        lines.append(f"freeAfter {volume.freeSectors()} {volume.freeSlotCount()}")
        (outDir / f"{stem}.manifest").write_text("\n".join(lines) + "\n")


def printable(name):
    return len(name) > 0 and all(0x20 <= ord(c) < 0x7F for c in name)


def dumpable(name):
    return printable(name) and "/" not in name and "?" not in name


def vtocFree(image):
    vtoc = image.readSector(17, 0)
    total = sum(bin(vtoc[0x38 + 4 * t + b]).count("1") for t in range(TRACKS) for b in (0, 1))
    return total - (vtoc[0x38 + 1] & 1)


def countFreeSlots(image, volume):
    track, sector = volume.catalogTrack, volume.catalogSector
    seen = set()
    free = 0
    while track != 0 and (track, sector) not in seen:
        seen.add((track, sector))
        buffer = image.readSector(track, sector)
        free += sum(1 for i in range(7) if buffer[ENTRY_OFFSET + ENTRY_SIZE * i] in (0, 0xFF))
        track, sector = buffer[1], buffer[2]
    return free


def real(outDir):
    outDir.mkdir(parents=True, exist_ok=True)
    candidates = [root / "sdcard/retro/images/apple2/test1.dsk"]
    for pattern in ("sdcard/retro/images/apple2/*.dsk", "sdcard/retro/images/apple2/*.do",
                    "assets/apple2Downloads/appleDisks/*.dsk"):
        candidates += sorted(root.glob(pattern))
    index = 0
    seenPaths = set()
    for path in candidates:
        if not path.is_file() or path in seenPaths or path.stat().st_size != IMAGE_SIZE:
            continue
        seenPaths.add(path)
        try:
            image = x.DiskImage(path)
            volume = x.openVolume(image)
        except x.ToolError:
            continue
        if not isinstance(volume, x.Dos33Volume):
            continue
        entries = volume.listFiles()
        names = [e.path.upper() for e in entries]
        dumpDir = outDir / f"real-{index}.files"
        dumpDir.mkdir(exist_ok=True)
        lines = [f"image {path.relative_to(root)}", f"ext {path.suffix[1:]}"]
        for entry, upper in zip(entries, names):
            if not dumpable(entry.path) or names.count(upper) > 1:
                lines.append(f"other {index}")
                continue
            try:
                exported = volume.exportFile(entry)
            except x.ToolError:
                lines.append(f"other {index}")
                continue
            (dumpDir / entry.path).write_bytes(exported.data)
            locked = int(entry.typeText.endswith("*"))
            lines.append(f"file {entry.path}|{exported.prodosType}|{exported.prodosAux}|{locked}|{len(exported.data)}")
        lines.append(f"free {vtocFree(image)} {countFreeSlots(image, volume)}")
        (outDir / f"real-{index}.manifest").write_text("\n".join(lines) + "\n")
        index += 1


def checkVolume(image, label, reservedTrack0):
    """Strict structural check of a DOS 3.3 image written by the C adapter."""
    vtoc = image.readSector(17, 0)
    assert vtoc[3] in (1, 2, 3) and vtoc[0x27] == PAIRS and vtoc[0x34] == TRACKS and vtoc[0x35] == SECTORS, label
    claimed = {(17, 0)}
    track, sector = vtoc[1], vtoc[2]
    seen = set()
    entries = []
    while track != 0:
        assert (track, sector) not in seen and 1 <= track < TRACKS and sector < SECTORS, f"{label}: catalog chain"
        seen.add((track, sector))
        claimed.add((track, sector))
        buffer = image.readSector(track, sector)
        for slot in range(7):
            entries.append(bytes(buffer[ENTRY_OFFSET + ENTRY_SIZE * slot:ENTRY_OFFSET + ENTRY_SIZE * (slot + 1)]))
        track, sector = buffer[1], buffer[2]
    names = set()
    for entry in entries:
        if entry[0] in (0, 0xFF):
            continue
        name = decodeName(entry).upper()
        assert name not in names, f"{label}: duplicate name {name}"
        names.add(name)
        owned = []
        track, sector = entry[0], entry[1]
        listSeen = set()
        while track != 0:
            assert (track, sector) not in listSeen and track < TRACKS and sector < SECTORS, f"{label}: {name} list chain"
            listSeen.add((track, sector))
            owned.append((track, sector))
            buffer = image.readSector(track, sector)
            for pair in range(PAIRS):
                dataTrack, dataSector = buffer[0x0C + 2 * pair], buffer[0x0D + 2 * pair]
                if (dataTrack, dataSector) != (0, 0):
                    assert dataTrack < TRACKS and dataSector < SECTORS, f"{label}: {name} pointer outside"
                    owned.append((dataTrack, dataSector))
            track, sector = buffer[1], buffer[2]
        assert struct.unpack_from("<H", entry, 33)[0] == len(owned), f"{label}: {name} sector count"
        for item in owned:
            assert item not in claimed, f"{label}: sector {item} claimed twice"
            claimed.add(item)
    for track in range(TRACKS):
        for sector in range(SECTORS):
            free = bool(vtoc[0x38 + 4 * track + (0 if sector >= 8 else 1)] & (1 << (sector & 7)))
            if (track, sector) in claimed:
                assert not free, f"{label}: sector {track}/{sector} used but marked free"
            elif track == CATALOG_TRACK or (track == 0 and reservedTrack0):
                assert not free, f"{label}: reserved sector {track}/{sector} marked free"
            else:
                assert free, f"{label}: sector {track}/{sector} leaked"


def verify(outDir):
    checked = 0
    for filesDir in sorted(outDir.glob("*.files")):
        imagePath = outDir / filesDir.name[:-len(".files")]
        if not imagePath.is_file():
            continue
        image = x.DiskImage(imagePath)
        volume = x.openVolume(image)
        assert isinstance(volume, x.Dos33Volume), f"{imagePath.name}: not a DOS 3.3 volume"
        if not imagePath.name.startswith("real"):
            checkVolume(image, imagePath.name, "t0" not in imagePath.name)
        entries = volume.listFiles()
        dumped = sorted(f.name for f in filesDir.iterdir() if f.name != "free.txt")
        listed = sorted(e.path for e in entries if dumpable(e.path))
        assert listed == dumped, f"{imagePath.name}: file list differs"
        for entry in entries:
            if not dumpable(entry.path):
                continue
            assert volume.exportFile(entry).data == (filesDir / entry.path).read_bytes(), f"{imagePath.name}: {entry.path} differs"
        freeSectors, freeSlots = [int(item) for item in (filesDir / "free.txt").read_text().split()]
        assert freeSectors == vtocFree(image), f"{imagePath.name}: free sectors differ"
        assert freeSlots == countFreeSlots(image, volume), f"{imagePath.name}: free slots differ"
        checked += 1
    assert checked > 0, "no images to verify"
    print(f"verified {checked} DOS 3.3 images")


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ("build", "real", "verify"):
        print(__doc__)
        return 2
    {"build": build, "real": real, "verify": verify}[sys.argv[1]](Path(sys.argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
