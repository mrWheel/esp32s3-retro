#!/usr/bin/env python3
"""List files on an Apple II disk image (DOS 3.3, ProDOS or UCSD Pascal) and
copy them to a ProDOS or UCSD Pascal destination image.

usage: extractApple2Files.py <orgDiskImage> <destDiskImage>

commands:
  L(ist [dest]                list the files on the source (or destination) image
  C(opy <nameIn> [<nameOut>]  copy a file from source to destination;
                              <nameIn> may contain wildcards (* ? [abc]),
                              <nameOut> is not allowed together with wildcards
  Q(uit
Commands may be abbreviated and are case-insensitive.
"""

import datetime
import fnmatch
import shlex
import sys
from dataclasses import dataclass, field
from pathlib import Path

BLOCK_SIZE = 512
DOS_IMAGE_SIZE = 143360

#-- ProDOS block half (0..15 within a track) to DOS 3.3 logical sector
PRODOS_TO_DOS_SECTOR = [0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15]

PRODOS_TYPE_NAMES = {
  0x00: "NON", 0x01: "BAD", 0x04: "TXT", 0x06: "BIN", 0x0B: "PCD", 0x0C: "PTX",
  0x0D: "PDA", 0x0F: "DIR", 0x19: "ADB", 0x1A: "AWP", 0x1B: "ASP", 0xB0: "SRC",
  0xB3: "S16", 0xEF: "PAS", 0xF0: "CMD", 0xFA: "INT", 0xFB: "IVR", 0xFC: "BAS",
  0xFD: "VAR", 0xFE: "REL", 0xFF: "SYS",
}
DOS_TYPE_NAMES = {0x00: "T", 0x01: "I", 0x02: "A", 0x04: "B", 0x08: "S", 0x10: "R", 0x20: "A2", 0x40: "B2"}
PASCAL_KIND_NAMES = {1: "BAD", 2: "CODE", 3: "TEXT", 4: "INFO", 5: "DATA", 6: "GRAF", 7: "FOTO", 8: "SECDIR"}
PASCAL_KIND_FROM_PRODOS = {0x0B: 2, 0x0C: 3, 0x0D: 5}
PRODOS_TYPE_FROM_PASCAL = {2: 0x0B, 3: 0x0C, 5: 0x0D}


class ToolError(Exception):
  pass


def u16(buf, off):
  return buf[off] | (buf[off + 1] << 8)


def u24(buf, off):
  return buf[off] | (buf[off + 1] << 8) | (buf[off + 2] << 16)


def blocksFor(length):
  return (length + BLOCK_SIZE - 1) // BLOCK_SIZE


@dataclass
class Entry:
  path: str
  typeText: str
  blocks: int
  length: int
  modified: str = ""
  isDir: bool = False
  meta: dict = field(default_factory=dict)


@dataclass
class Exported:
  data: bytes
  prodosType: int
  prodosAux: int
  pascalKind: int


class DiskImage:
  def __init__(self, path):
    self.path = Path(path).expanduser()
    if not self.path.is_file():
      raise ToolError(f"image not found: {self.path}")
    self.data = bytearray(self.path.read_bytes())
    self.order = "prodos"

  @property
  def blockCount(self):
    return len(self.data) // BLOCK_SIZE

  def sectorOffset(self, track, dosSector):
    if self.order == "dos":
      return (track * 16 + dosSector) * 256
    return (track * 16 + PRODOS_TO_DOS_SECTOR.index(dosSector)) * 256

  def readSector(self, track, sector):
    off = self.sectorOffset(track, sector)
    if off + 256 > len(self.data) or track > 34 or sector > 15:
      raise ToolError(f"bad track/sector {track}/{sector}")
    return bytes(self.data[off:off + 256])

  def blockOffsets(self, block):
    if block < 0 or block >= self.blockCount:
      raise ToolError(f"block {block} outside image")
    if self.order == "prodos":
      return [block * BLOCK_SIZE]
    track = block // 8
    half = (block % 8) * 2
    return [self.sectorOffset(track, PRODOS_TO_DOS_SECTOR[half]), self.sectorOffset(track, PRODOS_TO_DOS_SECTOR[half + 1])]

  def readBlock(self, block):
    offsets = self.blockOffsets(block)
    if len(offsets) == 1:
      return bytes(self.data[offsets[0]:offsets[0] + BLOCK_SIZE])
    return bytes(self.data[offsets[0]:offsets[0] + 256]) + bytes(self.data[offsets[1]:offsets[1] + 256])

  def writeBlock(self, block, buf):
    if len(buf) != BLOCK_SIZE:
      raise ToolError("internal error: block must be 512 bytes")
    offsets = self.blockOffsets(block)
    if len(offsets) == 1:
      self.data[offsets[0]:offsets[0] + BLOCK_SIZE] = buf
    else:
      self.data[offsets[0]:offsets[0] + 256] = buf[:256]
      self.data[offsets[1]:offsets[1] + 256] = buf[256:]

  def save(self):
    self.path.write_bytes(bytes(self.data))


def prodosDateText(dateWord, minute, hour):
  if dateWord == 0:
    return ""
  year = (dateWord >> 9) & 0x7F
  month = (dateWord >> 5) & 0x0F
  day = dateWord & 0x1F
  year += 2000 if year < 40 else 1900
  return f"{year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}"


def prodosNow():
  now = datetime.datetime.now()
  dateWord = ((now.year % 100) << 9) | (now.month << 5) | now.day
  return bytes([dateWord & 0xFF, dateWord >> 8, now.minute, now.hour])


def sameName(a, b):
  return a.upper() == b.upper()


#-- ---------------------------------------------------------------- DOS 3.3

class Dos33Volume:
  fsName = "DOS 3.3"
  writable = False

  def __init__(self, image):
    self.image = image
    vtoc = image.readSector(17, 0)
    self.volumeNumber = vtoc[6]
    self.catalogTrack = vtoc[1]
    self.catalogSector = vtoc[2]

  @staticmethod
  def probe(image):
    if len(image.data) != DOS_IMAGE_SIZE:
      return False
    vtoc = image.readSector(17, 0)
    return (vtoc[3] in (1, 2, 3) and vtoc[0x27] == 122 and vtoc[0x34] == 35 and vtoc[0x35] == 16
            and 1 <= vtoc[1] <= 34 and vtoc[2] <= 15)

  def description(self):
    return f"DOS 3.3 volume {self.volumeNumber}"

  def listFiles(self):
    entries = []
    track, sector = self.catalogTrack, self.catalogSector
    seen = set()
    while track != 0 and (track, sector) not in seen:
      seen.add((track, sector))
      buf = self.image.readSector(track, sector)
      for i in range(7):
        off = 0x0B + 35 * i
        tsTrack = buf[off]
        if tsTrack == 0 or tsTrack == 0xFF:
          continue
        rawType = buf[off + 2]
        name = bytes(b & 0x7F for b in buf[off + 3:off + 33]).decode("ascii", "replace").rstrip()
        sectors = u16(buf, off + 33)
        entry = Entry(name, DOS_TYPE_NAMES.get(rawType & 0x7F, f"${rawType & 0x7F:02X}") + ("*" if rawType & 0x80 else ""),
                      (sectors + 1) // 2, 0, "", False,
                      {"tsTrack": tsTrack, "tsSector": buf[off + 1], "rawType": rawType & 0x7F})
        try:
          entry.length = len(self.exportFile(entry).data)
        except ToolError:
          entry.length = sectors * 256
        entries.append(entry)
      track, sector = buf[1], buf[2]
    return entries

  def readRaw(self, entry):
    track, sector = entry.meta["tsTrack"], entry.meta["tsSector"]
    sectors = []
    seen = set()
    while track != 0 and (track, sector) not in seen:
      seen.add((track, sector))
      ts = self.image.readSector(track, sector)
      for i in range(122):
        t, s = ts[0x0C + 2 * i], ts[0x0D + 2 * i]
        sectors.append(None if (t == 0 and s == 0) else (t, s))
      track, sector = ts[1], ts[2]
    while sectors and sectors[-1] is None:
      sectors.pop()
    return b"".join(bytes(256) if ts is None else self.image.readSector(*ts) for ts in sectors)

  def exportFile(self, entry):
    raw = self.readRaw(entry)
    kind = entry.meta["rawType"]
    if kind in (0x01, 0x02):
      if len(raw) < 2:
        raise ToolError(f"{entry.path}: file too short")
      length = u16(raw, 0)
      return Exported(raw[2:2 + length], 0xFA if kind == 0x01 else 0xFC, 0x0800 if kind == 0x01 else 0x0801, 5)
    if kind == 0x04:
      if len(raw) < 4:
        raise ToolError(f"{entry.path}: file too short")
      address, length = u16(raw, 0), u16(raw, 2)
      return Exported(raw[4:4 + length], 0x06, address, 5)
    if kind == 0x00:
      return Exported(raw.rstrip(b"\x00"), 0x04, 0, 5)
    if kind == 0x10:
      return Exported(raw, 0xFE, 0, 5)
    return Exported(raw, 0x06, 0, 5)


#-- ---------------------------------------------------------------- ProDOS

def sanitizeProDosName(name):
  cleaned = "".join(c for c in name.upper().replace(" ", ".").replace("_", ".") if c.isalnum() and c.isascii() or c == ".")
  if not cleaned or not cleaned[0].isalpha():
    cleaned = "A" + cleaned
  return cleaned[:15]


class ProDosVolume:
  fsName = "ProDOS"
  writable = True

  def __init__(self, image):
    self.image = image
    header = image.readBlock(2)
    self.name = header[5:5 + (header[4] & 0x0F)].decode("ascii", "replace")
    self.bitmapStart = u16(header, 4 + 35)
    self.totalBlocks = min(u16(header, 4 + 37), image.blockCount)

  @staticmethod
  def probe(image):
    if image.blockCount < 8:
      return False
    block = image.readBlock(2)
    nameLength = block[4] & 0x0F
    return (u16(block, 0) == 0 and block[4] >> 4 == 0xF and 1 <= nameLength <= 15
            and block[4 + 31] == 0x27 and block[4 + 32] == 0x0D)

  def description(self):
    return f"ProDOS volume /{self.name}, {self.totalBlocks} blocks, {self.freeBlocks()} free"

  def bitmapBlockCount(self):
    return (self.totalBlocks + 4095) // 4096

  def readBitmap(self):
    return bytearray(b"".join(self.image.readBlock(self.bitmapStart + i) for i in range(self.bitmapBlockCount())))

  def freeBlocks(self):
    bitmap = self.readBitmap()
    return sum(1 for b in range(self.totalBlocks) if bitmap[b >> 3] & (0x80 >> (b & 7)))

  def iterDirectory(self, keyBlock):
    """Yield (block, offset, buffer) for every used entry in a directory."""
    block = keyBlock
    first = True
    seen = set()
    while block != 0 and block not in seen:
      seen.add(block)
      buf = self.image.readBlock(block)
      for i in range(13):
        if first and i == 0:
          continue
        off = 4 + 39 * i
        if buf[off] >> 4 != 0:
          yield block, off, buf
      first = False
      block = u16(buf, 2)

  def parseEntry(self, buf, off, prefix):
    storage = buf[off] >> 4
    name = buf[off + 1:off + 1 + (buf[off] & 0x0F)].decode("ascii", "replace")
    fileType = buf[off + 16]
    eof = u24(buf, off + 21)
    typeText = PRODOS_TYPE_NAMES.get(fileType, f"${fileType:02X}")
    modified = prodosDateText(u16(buf, off + 33), buf[off + 35], buf[off + 36])
    return Entry(prefix + name, typeText, u16(buf, off + 19), eof, modified, storage == 0xD,
                 {"storage": storage, "key": u16(buf, off + 17), "fileType": fileType, "aux": u16(buf, off + 31)})

  def listFiles(self, keyBlock=2, prefix=""):
    entries = []
    for _block, off, buf in self.iterDirectory(keyBlock):
      entry = self.parseEntry(buf, off, prefix)
      entries.append(entry)
      if entry.isDir:
        entries.extend(self.listFiles(entry.meta["key"], entry.path + "/"))
    return entries

  def readData(self, entry):
    storage, key, eof = entry.meta["storage"], entry.meta["key"], entry.length
    needed = blocksFor(eof)
    pointers = []
    if storage == 1:
      pointers = [key]
    elif storage == 2:
      pointers = self.indexPointers(key)
    elif storage == 3:
      master = self.image.readBlock(key)
      for i in range(128):
        indexBlock = master[i] | (master[256 + i] << 8)
        pointers.extend(self.indexPointers(indexBlock) if indexBlock else [0] * 256)
    else:
      raise ToolError(f"{entry.path}: unsupported storage type {storage}")
    chunks = [self.image.readBlock(p) if p else bytes(BLOCK_SIZE) for p in pointers[:needed]]
    return b"".join(chunks)[:eof]

  def indexPointers(self, indexBlock):
    buf = self.image.readBlock(indexBlock)
    return [buf[i] | (buf[256 + i] << 8) for i in range(256)]

  def exportFile(self, entry):
    if entry.isDir:
      raise ToolError(f"{entry.path} is a directory; directories cannot be copied")
    fileType = entry.meta["fileType"]
    return Exported(self.readData(entry), fileType, entry.meta["aux"], PASCAL_KIND_FROM_PRODOS.get(fileType, 5))

  #-- writing

  def findDirectory(self, parts):
    key = 2
    for part in parts:
      match = None
      for _block, off, buf in self.iterDirectory(key):
        entry = self.parseEntry(buf, off, "")
        if entry.isDir and sameName(entry.path, part):
          match = entry
          break
      if match is None:
        raise ToolError(f"directory '{part}' does not exist on the destination")
      key = match.meta["key"]
    return key

  def findFreeSlot(self, keyBlock):
    block = keyBlock
    first = True
    seen = set()
    while block != 0 and block not in seen:
      seen.add(block)
      buf = self.image.readBlock(block)
      for i in range(13):
        if first and i == 0:
          continue
        off = 4 + 39 * i
        if buf[off] >> 4 == 0:
          return block, off
      first = False
      block = u16(buf, 2)
    return None

  def importFile(self, outPath, exported):
    parts = [p for p in outPath.split("/") if p]
    if not parts:
      raise ToolError("empty destination name")
    fileName = sanitizeProDosName(parts[-1])
    dirKey = self.findDirectory(parts[:-1])
    for _block, off, buf in self.iterDirectory(dirKey):
      if sameName(self.parseEntry(buf, off, "").path, fileName):
        raise ToolError(f"'{fileName}' already exists on the destination")
    slot = self.findFreeSlot(dirKey)
    if slot is None:
      raise ToolError("destination directory is full")

    data = exported.data
    dataBlocks = max(1, blocksFor(len(data)))
    if dataBlocks > 32768:
      raise ToolError("file too large for ProDOS")
    if dataBlocks == 1:
      storage, indexCount, masterCount = 1, 0, 0
    elif dataBlocks <= 256:
      storage, indexCount, masterCount = 2, 1, 0
    else:
      storage, indexCount, masterCount = 3, (dataBlocks + 255) // 256, 1
    total = dataBlocks + indexCount + masterCount

    bitmap = self.readBitmap()
    free = [b for b in range(self.totalBlocks) if bitmap[b >> 3] & (0x80 >> (b & 7))]
    if len(free) < total:
      raise ToolError(f"not enough room on the destination ({total} blocks needed, {len(free)} free)")
    allocated = free[:total]
    for b in allocated:
      bitmap[b >> 3] &= ~(0x80 >> (b & 7)) & 0xFF

    master = allocated[0] if masterCount else None
    indexBlocks = allocated[masterCount:masterCount + indexCount]
    dataList = allocated[masterCount + indexCount:]
    for i, b in enumerate(dataList):
      chunk = data[i * BLOCK_SIZE:(i + 1) * BLOCK_SIZE]
      self.image.writeBlock(b, chunk + bytes(BLOCK_SIZE - len(chunk)))
    for n, ib in enumerate(indexBlocks):
      buf = bytearray(BLOCK_SIZE)
      for i, b in enumerate(dataList[n * 256:(n + 1) * 256]):
        buf[i], buf[256 + i] = b & 0xFF, b >> 8
      self.image.writeBlock(ib, bytes(buf))
    if master is not None:
      buf = bytearray(BLOCK_SIZE)
      for i, ib in enumerate(indexBlocks):
        buf[i], buf[256 + i] = ib & 0xFF, ib >> 8
      self.image.writeBlock(master, bytes(buf))
    key = master if master is not None else (indexBlocks[0] if indexBlocks else dataList[0])

    for i in range(self.bitmapBlockCount()):
      self.image.writeBlock(self.bitmapStart + i, bytes(bitmap[i * BLOCK_SIZE:(i + 1) * BLOCK_SIZE]))

    stamp = prodosNow()
    entry = bytearray(39)
    nameBytes = fileName.encode("ascii")
    entry[0] = (storage << 4) | len(nameBytes)
    entry[1:1 + len(nameBytes)] = nameBytes
    entry[16] = exported.prodosType & 0xFF
    entry[17], entry[18] = key & 0xFF, key >> 8
    entry[19], entry[20] = total & 0xFF, total >> 8
    entry[21], entry[22], entry[23] = len(data) & 0xFF, (len(data) >> 8) & 0xFF, len(data) >> 16
    entry[24:28] = stamp
    entry[30] = 0xC3
    entry[31], entry[32] = exported.prodosAux & 0xFF, (exported.prodosAux >> 8) & 0xFF
    entry[33:37] = stamp
    entry[37], entry[38] = dirKey & 0xFF, dirKey >> 8
    slotBlock, slotOff = slot
    buf = bytearray(self.image.readBlock(slotBlock))
    buf[slotOff:slotOff + 39] = entry
    self.image.writeBlock(slotBlock, bytes(buf))

    header = bytearray(self.image.readBlock(dirKey))
    count = u16(header, 4 + 33) + 1
    header[4 + 33], header[4 + 34] = count & 0xFF, count >> 8
    self.image.writeBlock(dirKey, bytes(header))
    return fileName


#-- ---------------------------------------------------------------- UCSD Pascal

def sanitizePascalName(name):
  cleaned = "".join(c for c in name.upper() if c.isascii() and c.isprintable() and c not in " $=?,[#:/")
  if not cleaned:
    raise ToolError("invalid Pascal file name")
  return cleaned[:15]


class PascalVolume:
  fsName = "UCSD Pascal"
  writable = True

  def __init__(self, image):
    self.image = image
    header = image.readBlock(2)
    self.dirEnd = u16(header, 2)
    self.name = header[7:7 + header[6]].decode("ascii", "replace")
    self.totalBlocks = min(u16(header, 14), image.blockCount)

  @staticmethod
  def probe(image):
    if image.blockCount < 8:
      return False
    block = image.readBlock(2)
    return (u16(block, 0) == 0 and 3 <= u16(block, 2) <= 32 and u16(block, 4) == 0 and 1 <= block[6] <= 7
            and 6 <= u16(block, 14) and u16(block, 16) <= 77)

  def description(self):
    used = sum(e.blocks for e in self.listFiles())
    free = self.totalBlocks - self.dirEnd - used
    return f"Pascal volume {self.name}:, {self.totalBlocks} blocks, ~{free} free"

  def readDirectory(self):
    raw = b"".join(self.image.readBlock(b) for b in range(2, self.dirEnd))
    count = u16(raw, 16)
    entries = []
    for i in range(count):
      off = 26 * (i + 1)
      if off + 26 > len(raw):
        break
      entries.append({
        "first": u16(raw, off), "next": u16(raw, off + 2), "kind": u16(raw, off + 4) & 0x0F,
        "name": raw[off + 7:off + 7 + raw[off + 6]].decode("ascii", "replace"),
        "lastBytes": u16(raw, off + 22), "date": u16(raw, off + 24),
      })
    return raw, entries

  def listFiles(self):
    _raw, entries = self.readDirectory()
    result = []
    for e in entries:
      blocks = e["next"] - e["first"]
      length = max(0, (blocks - 1) * BLOCK_SIZE + e["lastBytes"])
      month, day, year = e["date"] & 0x0F, (e["date"] >> 4) & 0x1F, (e["date"] >> 9) & 0x7F
      modified = f"{2000 + year if year < 40 else 1900 + year:04d}-{month:02d}-{day:02d}" if e["date"] else ""
      result.append(Entry(e["name"], PASCAL_KIND_NAMES.get(e["kind"], f"#{e['kind']}"), blocks, length, modified, False, e))
    return result

  def exportFile(self, entry):
    e = entry.meta
    data = b"".join(self.image.readBlock(b) for b in range(e["first"], e["next"]))[:entry.length]
    return Exported(data, PRODOS_TYPE_FROM_PASCAL.get(e["kind"], 0x06), 0, e["kind"])

  def importFile(self, outPath, exported):
    if "/" in outPath:
      raise ToolError("Pascal volumes have no directories; '/' is not allowed in the name")
    fileName = sanitizePascalName(outPath)
    raw, entries = self.readDirectory()
    if any(sameName(e["name"], fileName) for e in entries):
      raise ToolError(f"'{fileName}' already exists on the destination")
    capacity = len(raw) // 26 - 1
    if len(entries) >= capacity:
      raise ToolError("destination directory is full")

    data = exported.data
    needed = max(1, blocksFor(len(data)))
    position = None
    cursor = self.dirEnd
    ordered = sorted(entries, key=lambda e: e["first"])
    for e in ordered:
      if e["first"] - cursor >= needed:
        position = cursor
        break
      cursor = max(cursor, e["next"])
    if position is None and self.totalBlocks - cursor >= needed:
      position = cursor
    if position is None:
      raise ToolError(f"not enough contiguous room on the destination ({needed} blocks needed)")

    for i in range(needed):
      chunk = data[i * BLOCK_SIZE:(i + 1) * BLOCK_SIZE]
      self.image.writeBlock(position + i, chunk + bytes(BLOCK_SIZE - len(chunk)))

    now = datetime.datetime.now()
    kind = exported.pascalKind
    ordered.append({
      "first": position, "next": position + needed, "kind": kind, "name": fileName,
      "lastBytes": len(data) - (needed - 1) * BLOCK_SIZE if data else 0,
      "date": now.month | (now.day << 4) | ((now.year % 100) << 9),
    })
    ordered.sort(key=lambda e: e["first"])

    out = bytearray(raw)
    out[16], out[17] = len(ordered) & 0xFF, len(ordered) >> 8
    for i in range(capacity):
      out[26 * (i + 1):26 * (i + 2)] = bytes(26)
    for i, e in enumerate(ordered):
      off = 26 * (i + 1)
      nameBytes = e["name"].encode("ascii", "replace")
      out[off], out[off + 1] = e["first"] & 0xFF, e["first"] >> 8
      out[off + 2], out[off + 3] = e["next"] & 0xFF, e["next"] >> 8
      out[off + 4], out[off + 5] = e["kind"] & 0xFF, 0
      out[off + 6] = len(nameBytes)
      out[off + 7:off + 7 + len(nameBytes)] = nameBytes
      out[off + 22], out[off + 23] = e["lastBytes"] & 0xFF, e["lastBytes"] >> 8
      out[off + 24], out[off + 25] = e["date"] & 0xFF, e["date"] >> 8
    for i, b in enumerate(range(2, self.dirEnd)):
      self.image.writeBlock(b, bytes(out[i * BLOCK_SIZE:(i + 1) * BLOCK_SIZE]))
    return fileName


#-- ---------------------------------------------------------------- program

def openVolume(image):
  if len(image.data) == DOS_IMAGE_SIZE:
    suffix = image.path.suffix.lower()
    orders = ["prodos", "dos"] if suffix == ".po" else ["dos", "prodos"]
  else:
    orders = ["prodos"]
  for order in orders:
    image.order = order
    for volumeClass in (ProDosVolume, PascalVolume, Dos33Volume):
      try:
        if volumeClass.probe(image):
          return volumeClass(image)
      except ToolError:
        continue
  raise ToolError(f"{image.path.name}: not a recognised DOS 3.3, ProDOS or UCSD Pascal image")


def printListing(volume):
  print(volume.description())
  entries = volume.listFiles()
  if not entries:
    print("  (no files)")
    return
  width = max(20, max(len(e.path) for e in entries))
  print(f"  {'Name':<{width}} {'Type':<5} {'Blocks':>6} {'Length':>8}  Modified")
  for e in entries:
    print(f"  {e.path:<{width}} {e.typeText:<5} {e.blocks:>6} {e.length:>8}  {e.modified}")
  print(f"  {len(entries)} entries")


def findEntry(volume, name):
  for e in volume.listFiles():
    if sameName(e.path, name):
      return e
  raise ToolError(f"'{name}' not found on the source")


def copyFile(source, dest, nameIn, nameOut):
  if any(c in nameIn for c in "*?["):
    if nameOut:
      raise ToolError("<nameOut> cannot be combined with a wildcard in <nameIn>")
    pattern = nameIn.upper()
    matches = [e for e in source.listFiles() if fnmatch.fnmatchcase(e.path.upper(), pattern)]
    if not matches:
      raise ToolError(f"no files match '{nameIn}'")
    copied = 0
    for entry in matches:
      try:
        copyOne(source, dest, entry, None)
        copied += 1
      except ToolError as error:
        print(f"skipped '{entry.path}': {error}")
    print(f"{copied} of {len(matches)} files copied")
    return
  copyOne(source, dest, findEntry(source, nameIn), nameOut)


def copyOne(source, dest, entry, nameOut):
  exported = source.exportFile(entry)
  target = dest.importFile(nameOut or entry.path, exported)
  dest.image.save()
  print(f"copied '{entry.path}' -> '{target}' ({len(exported.data)} bytes)")


def matchCommand(word):
  for name in ("list", "copy", "quit"):
    if name.startswith(word.lower()):
      return name
  return None


def main(argv):
  if len(argv) != 3:
    print(__doc__)
    return 1
  try:
    sourceImage = DiskImage(argv[1])
    destPath = Path(argv[2]).expanduser()
    destImage = sourceImage if destPath.exists() and destPath.samefile(sourceImage.path) else DiskImage(destPath)
    source = openVolume(sourceImage)
    dest = source if destImage is sourceImage else openVolume(destImage)
    if destImage is sourceImage:
      dest = type(source)(destImage)
    if not dest.writable:
      raise ToolError("destination must be a ProDOS or UCSD Pascal image")
  except ToolError as error:
    print(f"error: {error}")
    return 1

  print(f"source:      {source.description()}")
  print(f"destination: {dest.description()}")
  while True:
    try:
      line = input("command (list, copy, quit): ")
    except EOFError:
      print()
      return 0
    try:
      words = shlex.split(line)
    except ValueError as error:
      print(f"error: {error}")
      continue
    if not words:
      continue
    command = matchCommand(words[0])
    try:
      if command == "quit":
        return 0
      if command == "list":
        printListing(dest if len(words) > 1 and "destination".startswith(words[1].lower()) else source)
      elif command == "copy" and len(words) in (2, 3):
        copyFile(source, dest, words[1], words[2] if len(words) == 3 else None)
      else:
        print("usage: L(ist [dest] | C(opy <nameIn> [<nameOut>] | Q(uit")
    except ToolError as error:
      print(f"error: {error}")


if __name__ == "__main__":
  sys.exit(main(sys.argv))
