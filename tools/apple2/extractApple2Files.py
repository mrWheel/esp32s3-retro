#!/usr/bin/env python3
"""Interactively browse, copy and remove files on Apple II disk images.

Run with Python 3. Select source/dest folders and disk images from the menu.
Choose n in the source disk list to copy files from the source folder.
Choose n in the destination disk list to extract files into the destination folder.
Relative paths are looked up from the current directory and the repository root.
"""

import datetime
import fnmatch
import os
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

try:
  import readline
except ImportError:
  readline = None

BLOCK_SIZE = 512
DOS_IMAGE_SIZE = 143360

#-- ProDOS block half (0..15 within a track) to DOS 3.3 logical sector
PRODOS_TO_DOS_SECTOR = [0, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 15]
DOS_SECTOR_OF_PHYSICAL = [0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15]
GCR_WRITE_TABLE = (
  0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
  0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
  0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
  0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
)

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
    imageData = self.path.read_bytes()
    self.wozData = None
    self.wozSectorOffsets = None
    self.wozWriteProtected = False
    self.twoImgData = None
    self.twoImgDataOffset = None
    self.twoImgDataLength = None
    self.twoImgWriteProtected = False
    self.twoImgOrder = None
    if imageData[:4] == b"2IMG":
      (self.twoImgData, self.data, self.twoImgDataOffset, self.twoImgDataLength,
       self.twoImgOrder, self.twoImgWriteProtected) = decodeTwoImg(imageData)
      self.twoImgBaseData = bytes(self.data)
    elif imageData[:4] in (b"WOZ1", b"WOZ2"):
      self.wozData, self.data, self.wozSectorOffsets, self.wozWriteProtected = decodeWoz(imageData)
      self.wozBaseData = bytes(self.data)
    else:
      self.data = bytearray(imageData)
    self.order = "prodos"

  @property
  def blockCount(self):
    return len(self.data) // BLOCK_SIZE

  def sectorOffset(self, track, dosSector):
    if self.wozData is not None:
      return (track * 16 + dosSector) * 256
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
    if self.wozData is not None and self.order == "prodos":
      track = block // 8
      half = (block % 8) * 2
      return [
        self.sectorOffset(track, PRODOS_TO_DOS_SECTOR[half]),
        self.sectorOffset(track, PRODOS_TO_DOS_SECTOR[half + 1]),
      ]
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
    if self.twoImgData is not None:
      if self.twoImgWriteProtected and bytes(self.data) != self.twoImgBaseData:
        raise ToolError("2IMG image is marked write-protected")
      imageData = bytearray(self.twoImgData)
      start = self.twoImgDataOffset
      imageData[start:start + self.twoImgDataLength] = self.data
      self.path.write_bytes(imageData)
      self.twoImgData = bytes(imageData)
      self.twoImgBaseData = bytes(self.data)
      return
    if self.wozData is not None:
      if self.wozWriteProtected and bytes(self.data) != self.wozBaseData:
        raise ToolError("WOZ image is marked write-protected")
      imageData = bytearray(self.wozData)
      for track in range(35):
        for dosSector in range(16):
          sector = track * 16 + dosSector
          start = sector * 256
          current = bytes(self.data[start:start + 256])
          original = self.wozBaseData[start:start + 256]
          if current == original:
            continue
          bitOffset = self.wozSectorOffsets.get((track, dosSector))
          if bitOffset is None:
            raise ToolError(f"WOZ track {track} is missing sector {dosSector}; cannot save changes")
          encoded = encodeWozSector(current)
          for index, value in enumerate(encoded):
            writeWozByte(imageData, bitOffset + index * 8, value)
      imageData[8:12] = (zlib.crc32(imageData[12:]) & 0xFFFFFFFF).to_bytes(4, "little")
      self.path.write_bytes(imageData)
      self.wozData = bytes(imageData)
      self.wozBaseData = bytes(self.data)
      return
    self.path.write_bytes(bytes(self.data))


def decodeTwoImg(imageData):
  if len(imageData) < 64:
    raise ToolError("invalid 2IMG header: header is shorter than 64 bytes")
  headerSize = int.from_bytes(imageData[8:10], "little")
  imageFormat = int.from_bytes(imageData[12:16], "little")
  flags = int.from_bytes(imageData[16:20], "little")
  blockCount = int.from_bytes(imageData[20:24], "little")
  dataOffset = int.from_bytes(imageData[24:28], "little")
  dataLength = int.from_bytes(imageData[28:32], "little")
  if headerSize < 64 or dataOffset < headerSize or dataOffset + dataLength > len(imageData):
    raise ToolError("invalid 2IMG header or data bounds")
  if imageFormat not in (0, 1):
    raise ToolError(f"unsupported 2IMG sector order {imageFormat}")
  if dataLength == 0 or dataLength % BLOCK_SIZE:
    raise ToolError("2IMG data length must contain whole 512-byte blocks")
  if blockCount and blockCount != dataLength // BLOCK_SIZE:
    raise ToolError("2IMG block count does not match its data length")
  imageDataBytes = bytearray(imageData[dataOffset:dataOffset + dataLength])
  return (bytes(imageData), imageDataBytes, dataOffset, dataLength, "dos" if imageFormat == 0 else "prodos",
          bool(flags & 0x80000000))


def readWozByte(data, bitOffset):
  byteOffset, shift = divmod(bitOffset, 8)
  if shift == 0:
    return data[byteOffset]
  return ((data[byteOffset] << 8 | data[(byteOffset + 1) % len(data)]) >> (8 - shift)) & 0xFF


def writeWozByte(data, bitOffset, value):
  for index in range(8):
    bit = (value >> (7 - index)) & 1
    position = bitOffset + index
    offset, bitIndex = divmod(position, 8)
    mask = 1 << (7 - bitIndex)
    data[offset] = (data[offset] | mask) if bit else (data[offset] & ~mask)


def decodeWozTrack(trackData, bitCount, expectedTrack, sectors, sectorOffsets, trackFileOffset):
  prologues = []
  window = 0
  for bitOffset in range(bitCount):
    byte = trackData[bitOffset >> 3]
    bit = (byte >> (7 - (bitOffset & 7))) & 1
    window = ((window << 1) | bit) & 0xFFFFFF
    if bitOffset >= 23:
      if window == 0xD5AA96:
        prologues.append((bitOffset - 23, 0x96))
      elif window == 0xD5AAAD:
        prologues.append((bitOffset - 23, 0xAD))

  dataPrologues = [position for position, kind in prologues if kind == 0xAD]
  addressPrologues = [position for position, kind in prologues if kind == 0x96]
  decodeTable = {value: index for index, value in enumerate(GCR_WRITE_TABLE)}
  for addressOffset in addressPrologues:
    headerOffset = addressOffset + 24
    if headerOffset + 64 >= bitCount:
      continue
    header = [readWozByte(trackData, headerOffset + index * 8) for index in range(8)]
    decoded = [((header[index] << 1) | 1) & header[index + 1] for index in (0, 2, 4, 6)]
    volume, track, sector, checksum = decoded
    if track != expectedTrack or sector >= 16 or checksum != (volume ^ track ^ sector):
      continue
    dataOffset = next(
      (position for position in dataPrologues if headerOffset + 88 <= position < headerOffset + 800),
      None,
    )
    if dataOffset is None:
      continue
    encoded = None
    fieldOffset = None
    for shift in range(24, 32):
      if dataOffset + shift + 343 * 8 >= bitCount:
        continue
      candidate = bytes(readWozByte(trackData, dataOffset + shift + index * 8) for index in range(343))
      if any(nibble not in decodeTable for nibble in candidate):
        continue
      previous = 0
      values = []
      for nibble in candidate[:342]:
        previous = decodeTable[nibble] ^ previous
        values.append(previous)
      epilogue = dataOffset + shift + 343 * 8
      if (decodeTable[candidate[342]] == previous and
          readWozByte(trackData, epilogue) == 0xDE and
          readWozByte(trackData, epilogue + 8) == 0xAA):
        encoded = candidate
        fieldOffset = dataOffset + shift
        decodedValues = values
        break
    if encoded is None:
      continue
    sectorData = bytearray(256)
    for index in range(256):
      reversedLow = (decodedValues[index % 86] >> (2 * (index // 86))) & 3
      low = ((reversedLow & 1) << 1) | (reversedLow >> 1)
      sectorData[index] = (decodedValues[86 + index] << 2) | low
    logicalSector = DOS_SECTOR_OF_PHYSICAL[sector]
    sectorIndex = expectedTrack * 16 + logicalSector
    start = sectorIndex * 256
    sectors[start:start + 256] = sectorData
    sectorOffsets[(expectedTrack, logicalSector)] = trackFileOffset * 8 + fieldOffset


def decodeWoz(imageData):
  if len(imageData) < 12 or imageData[4:8] != b"\xFF\x0A\x0D\x0A":
    raise ToolError("invalid WOZ image header")
  version = imageData[:4]
  chunks = {}
  offset = 12
  while offset + 8 <= len(imageData):
    tag = imageData[offset:offset + 4]
    length = int.from_bytes(imageData[offset + 4:offset + 8], "little")
    start = offset + 8
    end = start + length
    if end > len(imageData):
      raise ToolError("truncated WOZ image chunk")
    chunks[tag] = (start, end)
    offset = end
  if b"INFO" not in chunks or b"TMAP" not in chunks or b"TRKS" not in chunks:
    raise ToolError("WOZ image is missing its INFO, TMAP or TRKS chunk")

  infoStart, infoEnd = chunks[b"INFO"]
  info = imageData[infoStart:infoEnd]
  if len(info) != 60 or info[0] != int(version[-1:]):
    raise ToolError("invalid WOZ INFO chunk")

  tmapStart, tmapEnd = chunks[b"TMAP"]
  tmap = imageData[tmapStart:tmapEnd]
  if len(tmap) != 160:
    raise ToolError("invalid WOZ TMAP chunk")
  tracks = {}
  tracksStart, tracksEnd = chunks[b"TRKS"]
  if version == b"WOZ1":
    recordSize = 6656
    for index in range(min(160, (tracksEnd - tracksStart) // recordSize)):
      record = tracksStart + index * recordSize
      bytesUsed = int.from_bytes(imageData[record + 6646:record + 6648], "little")
      bitCount = int.from_bytes(imageData[record + 6648:record + 6650], "little")
      start = record
      if bytesUsed and bytesUsed <= 6646 and bitCount and bitCount <= bytesUsed * 8:
        tracks[index] = (imageData[start:start + bytesUsed], bitCount, start)
  else:
    if tracksEnd - tracksStart < 1280:
      raise ToolError("invalid WOZ2 TRKS chunk")
    for index in range(160):
      descriptor = tracksStart + index * 8
      startBlock = int.from_bytes(imageData[descriptor:descriptor + 2], "little")
      blockCount = int.from_bytes(imageData[descriptor + 2:descriptor + 4], "little")
      bitCount = int.from_bytes(imageData[descriptor + 4:descriptor + 8], "little")
      size = (bitCount + 7) // 8
      start = startBlock * 512
      if startBlock >= 3 and blockCount and bitCount and size <= blockCount * 512 and start + size <= len(imageData):
        tracks[index] = (imageData[start:start + size], bitCount, start)

  sectors = bytearray(35 * 16 * 256)
  sectorOffsets = {}
  for track in range(35):
    trackIndex = tmap[track * 4]
    if trackIndex == 0xFF or trackIndex not in tracks:
      continue
    trackData, bitCount, trackFileOffset = tracks[trackIndex]
    decodeWozTrack(trackData, min(bitCount, len(trackData) * 8), track, sectors, sectorOffsets, trackFileOffset)
  if len(sectorOffsets) < 35 * 16:
    raise ToolError("WOZ image does not contain a complete 35-track, 16-sector disk")
  return bytes(imageData), sectors, sectorOffsets, bool(info[2])


def encodeWozSector(data):
  values = [0] * 342
  for index, value in enumerate(data):
    low = value & 3
    reversedLow = ((low & 1) << 1) | (low >> 1)
    values[index % 86] |= reversedLow << (2 * (index // 86))
    values[86 + index] = value >> 2
  encoded = bytearray()
  previous = 0
  for value in values:
    encoded.append(GCR_WRITE_TABLE[value ^ previous])
    previous = value
  encoded.append(GCR_WRITE_TABLE[previous])
  return bytes(encoded)


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

  def parseEntry(self, buf, off, prefix, block=None):
    storage = buf[off] >> 4
    name = buf[off + 1:off + 1 + (buf[off] & 0x0F)].decode("ascii", "replace")
    fileType = buf[off + 16]
    eof = u24(buf, off + 21)
    typeText = PRODOS_TYPE_NAMES.get(fileType, f"${fileType:02X}")
    modified = prodosDateText(u16(buf, off + 33), buf[off + 35], buf[off + 36])
    return Entry(prefix + name, typeText, u16(buf, off + 19), eof, modified, storage == 0xD,
                 {"storage": storage, "key": u16(buf, off + 17), "fileType": fileType, "aux": u16(buf, off + 31),
                  "slotBlock": block, "slotOff": off})

  def listFiles(self, keyBlock=2, prefix=""):
    entries = []
    for block, off, buf in self.iterDirectory(keyBlock):
      entry = self.parseEntry(buf, off, prefix, block)
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

  def blocksOf(self, entry):
    storage, key = entry.meta["storage"], entry.meta["key"]
    blocks = [key]
    if storage == 2:
      blocks.extend(p for p in self.indexPointers(key) if p)
    elif storage == 3:
      master = self.image.readBlock(key)
      for i in range(128):
        indexBlock = master[i] | (master[256 + i] << 8)
        if indexBlock:
          blocks.append(indexBlock)
          blocks.extend(p for p in self.indexPointers(indexBlock) if p)
    elif storage != 1:
      raise ToolError(f"{entry.path}: unsupported storage type {storage}")
    if any(b >= self.totalBlocks for b in blocks):
      raise ToolError(f"{entry.path}: block pointer outside the volume (damaged file)")
    return blocks

  def removeFile(self, entry):
    if entry.isDir:
      raise ToolError(f"{entry.path} is a directory; directories cannot be removed")
    blocks = self.blocksOf(entry)

    #-- give the blocks back to the volume bitmap
    bitmap = self.readBitmap()
    for b in blocks:
      bitmap[b >> 3] |= 0x80 >> (b & 7)
    for i in range(self.bitmapBlockCount()):
      self.image.writeBlock(self.bitmapStart + i, bytes(bitmap[i * BLOCK_SIZE:(i + 1) * BLOCK_SIZE]))

    #-- free the directory slot (storage type 0) and lower the file count of its directory
    slotBlock, slotOff = entry.meta["slotBlock"], entry.meta["slotOff"]
    buf = bytearray(self.image.readBlock(slotBlock))
    headerKey = u16(buf, slotOff + 37)
    buf[slotOff] = 0
    self.image.writeBlock(slotBlock, bytes(buf))

    header = bytearray(self.image.readBlock(headerKey))
    count = max(0, u16(header, 4 + 33) - 1)
    header[4 + 33], header[4 + 34] = count & 0xFF, count >> 8
    self.image.writeBlock(headerKey, bytes(header))


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

    self.writeDirectory(raw, ordered)
    return fileName

  def writeDirectory(self, raw, ordered):
    capacity = len(raw) // 26 - 1
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

  def removeFile(self, entry):
    raw, entries = self.readDirectory()
    remaining = [e for e in entries
                 if not (e["first"] == entry.meta["first"] and sameName(e["name"], entry.meta["name"]))]
    if len(remaining) == len(entries):
      raise ToolError(f"'{entry.path}' not found on the destination")
    remaining.sort(key=lambda e: e["first"])
    self.writeDirectory(raw, remaining)


#-- ---------------------------------------------------------------- program

def openVolume(image):
  if image.twoImgData is not None:
    orders = [image.twoImgOrder]
  elif len(image.data) == DOS_IMAGE_SIZE:
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


class FolderSource:
  def __init__(self, directory):
    self.directory = Path(directory)

  def listFiles(self):
    try:
      files = sorted((path for path in self.directory.iterdir() if path.is_file()),
                     key=lambda path: path.name.casefold())
      return [Entry(path.name, "BIN", blocksFor(path.stat().st_size), path.stat().st_size)
              for path in files]
    except OSError as error:
      raise ToolError(f"cannot list source folder '{self.directory}': {error}") from error

  def exportFile(self, entry):
    if entry.isDir or Path(entry.path).name != entry.path or entry.path in (".", ".."):
      raise ToolError("invalid source file name")
    try:
      data = (self.directory / entry.path).read_bytes()
    except OSError as error:
      raise ToolError(f"cannot read '{entry.path}': {error}") from error
    # Host files have no Apple II metadata; import their bytes as BIN / Pascal DATA.
    return Exported(data, 0x06, 0, 5)


class FolderDestination:
  def __init__(self, directory):
    self.directory = Path(directory)

  def importFile(self, outPath, exported):
    # Extract into the selected folder, including files from image subdirectories.
    name = outPath.rsplit("/", 1)[-1]
    if name in ("", ".", "..") or "\\" in name or "\x00" in name:
      raise ToolError("invalid destination file name")
    target = self.directory / name
    try:
      with target.open("xb") as output:
        output.write(exported.data)
    except FileExistsError as error:
      raise ToolError(f"'{name}' already exists in the destination folder") from error
    except OSError as error:
      raise ToolError(f"cannot extract '{name}': {error}") from error
    return str(target)


def copyOne(source, dest, entry, nameOut):
  exported = source.exportFile(entry)
  target = dest.importFile(nameOut or entry.path, exported)
  if not isinstance(dest, FolderDestination):
    dest.image.save()
  print(f"copied '{entry.path}' -> '{target}' ({len(exported.data)} bytes)")


def askYesNo(question):
  try:
    answer = input(question)
  except EOFError:
    print()
    return False
  return answer.strip().lower() in ("y", "yes")


def removeFiles(dest, name):
  entries = dest.listFiles()
  diskName = dest.image.path.name
  if not entries:
    raise ToolError(f"the destination ({diskName}) has no files; R(emove only works on the destination")
  if any(c in name for c in "*?["):
    pattern = name.upper()
    matches = [e for e in entries if fnmatch.fnmatchcase(e.path.upper(), pattern)]
    if not matches:
      raise ToolError(f"no files match '{name}' on the destination ({diskName})")
  else:
    matches = [e for e in entries if sameName(e.path, name)]
    if not matches:
      raise ToolError(f"'{name}' not found on the destination ({diskName})")
  removed = 0
  for entry in matches:
    if entry.isDir:
      print(f"skipped '{entry.path}': directory")
      continue
    if not askYesNo(f"Remove {entry.path}? (y/N) "):
      continue
    try:
      dest.removeFile(entry)
      dest.image.save()
      removed += 1
    except ToolError as error:
      print(f"skipped '{entry.path}': {error}")
  print(f"{removed} of {len(matches)} files removed")


REPO_ROOT = Path(__file__).resolve().parents[2]
pathPromptActive = False
pathBaseDir = ""


def searchBases(text):
  given = os.path.expanduser(text)
  return [given, str(REPO_ROOT) + "/" + text.lstrip("/")] if text.startswith("/") else [given, str(REPO_ROOT) + "/" + text]


def displayDirectoryPath(path):
  path = Path(path).resolve()
  try:
    path.relative_to(REPO_ROOT)
  except ValueError:
    try:
      relative = path.relative_to(Path.home().resolve())
    except ValueError:
      return str(path)
    return f"~/{relative.as_posix()}" if relative.parts else "~"
  relative = path.relative_to(REPO_ROOT.parent)
  return f"./{relative.as_posix()}" if relative.parts else "."


def resolveDirectoryPath(text):
  for candidate in searchBases(text):
    if os.path.isdir(candidate):
      return Path(candidate).resolve()
  for candidate in searchBases(text):
    if os.path.isfile(candidate):
      raise ToolError(f"{text} is a file; enter a folder")
  raise ToolError(f"folder not found: {text}")


def resolveImagePath(text, baseDirectory=None):
  if baseDirectory is not None and not text.startswith(("/", "~")):
    bases = [str(baseDirectory / os.path.expanduser(text))]
  else:
    bases = searchBases(text)
  for candidate in bases:
    if os.path.isfile(candidate):
      return Path(candidate)
  for candidate in bases:
    if os.path.isdir(candidate):
      raise ToolError(f"{text} is a directory; add the name of the image")
  raise ToolError(f"image not found: {text}")


def pathMatches(partial):
  found = set()
  for base in searchBases(partial):
    folder, prefix = os.path.split(base)
    typedFolder = partial[:len(partial) - len(prefix)]
    try:
      names = os.listdir(folder or ".")
    except OSError:
      continue
    for name in names:
      if name.startswith(prefix):
        found.add(typedFolder + name + ("/" if os.path.isdir(os.path.join(folder, name)) else ""))
  return sorted(found)


def pathCompleter(text, state):
  partial = text
  if pathBaseDir and not partial.startswith(("/", "~")):
    matches = [m[len(pathBaseDir):] for m in pathMatches(pathBaseDir + partial)]
  else:
    matches = pathMatches(partial)
  return matches[state] if state < len(matches) else None


def setupLineEditing():
  if readline is None:
    return
  if "libedit" in (readline.__doc__ or ""):
    readline.parse_and_bind("bind ^I rl_complete")
  else:
    readline.parse_and_bind("tab: complete")
  readline.set_completer_delims("\n")
  readline.set_completer(pathCompleter)


def promptPath(label, baseDir=""):
  global pathPromptActive, pathBaseDir
  pathPromptActive = True
  pathBaseDir = os.path.join(baseDir, "") if baseDir else ""
  try:
    text = input(label).strip()
  finally:
    pathPromptActive = False
    pathBaseDir = ""
  if text and baseDir and not text.startswith(("/", "~")):
    text = os.path.join(baseDir, text)
  return text


def loadVolume(text, otherImage, baseDirectory=None, opener=None):
  path = resolveImagePath(text, baseDirectory)
  shared = otherImage if otherImage is not None and otherImage.path.samefile(path) else None
  if opener is not None:
    #-- other operating systems supply their own (path, shared image) -> (image, volume)
    return opener(path, shared)
  image = shared if shared is not None else DiskImage(path)
  return image, openVolume(image)


class Session:
  def __init__(self, ucsdMode=False, opener=None):
    self.sourceImage = None
    self.source = None
    self.destImage = None
    self.dest = None
    self.pathSource = None
    self.pathDestination = None
    self.extractToFolder = False
    self.copyFromFolder = False
    self.ucsdMode = ucsdMode
    self.opener = opener

  def showSource(self):
    label = "Copy File from Folder" if self.copyFromFolder else (self.sourceImage.path.name if self.sourceImage else '<empty>')
    print(f"2 - Source disk [{label}]")

  def showDestination(self):
    label = "Extract File to Folder" if self.extractToFolder else (self.destImage.path.name if self.destImage else '<empty>')
    print(f"4 - Dest. disk [{label}]")

  def showSourcePath(self):
    path = displayDirectoryPath(self.pathSource) if self.pathSource else '<empty>'
    print(f"1 - Source path [{path}]")

  def showDestinationPath(self):
    path = displayDirectoryPath(self.pathDestination) if self.pathDestination else '<empty>'
    print(f"3 - Dest. path [{path}]")

  def setSourcePath(self, text):
    self.pathSource = resolveDirectoryPath(text)

  def setDestinationPath(self, text):
    self.pathDestination = resolveDirectoryPath(text)

  def load(self, text, otherImage, baseDirectory):
    if self.opener is None:
      return loadVolume(text, otherImage, baseDirectory)
    return loadVolume(text, otherImage, baseDirectory, self.opener)

  def setSource(self, text):
    image, volume = self.load(text, self.destImage, self.pathSource)
    self.sourceImage, self.source = image, volume
    self.copyFromFolder = False

  def setFolderSource(self):
    if self.pathSource is None:
      raise ToolError("source path is empty; select it with menu option 1")
    self.sourceImage, self.source = None, None
    self.copyFromFolder = True

  def setDestination(self, text):
    image, volume = self.load(text, self.sourceImage, self.pathDestination)
    if not volume.writable:
      raise ToolError("dest must be a writable disk image")
    self.destImage, self.dest = image, volume
    self.extractToFolder = False

  def setFolderDestination(self):
    if self.pathDestination is None:
      raise ToolError("dest path is empty; select it with menu option 3")
    self.destImage, self.dest = None, None
    self.extractToFolder = True

  def listPathFiles(self, role):
    directory = self.pathSource if role == "source" else self.pathDestination
    if directory is None:
      pathName = "source" if role == "source" else "dest"
      raise ToolError(f"{pathName} path is empty; select it with menu option {'1' if role == 'source' else '3'}")
    try:
      files = sorted((path for path in directory.iterdir() if path.is_file()),
                     key=lambda path: path.name.casefold())
    except OSError as error:
      raise ToolError(f"cannot list {role} path '{directory}': {error}") from error
    return files

  def requireSource(self):
    if self.copyFromFolder:
      if self.pathSource is None:
        raise ToolError("source path is empty; select it with menu option 1")
      return FolderSource(self.pathSource)
    if self.source is None:
      raise ToolError("source disk is empty; select it with menu option 2")
    return self.source

  def requireDestination(self, forCopy=False):
    if self.extractToFolder:
      if forCopy:
        if self.pathDestination is None:
          raise ToolError("dest path is empty; select it with menu option 3")
        return FolderDestination(self.pathDestination)
      raise ToolError("destination is a folder; select a disk image with menu option 4 to list or remove files")
    if self.dest is None:
      raise ToolError("dest disk is empty; select it with menu option 4")
    return self.dest


def chooseNumber(items, prompt, allowName=False, allowFolder=False, folderLabel="Extract File to Folder"):
  if not items and not allowFolder:
    print("  (no files)")
    return None
  width = len(str(len(items)))
  for number, item in enumerate(items, 1):
    name = item.name if isinstance(item, Path) else item.path
    print(f"{number:>{width}}. {name}")
  if allowFolder:
    print(f"n - {folderLabel}")
  answer = input(prompt).strip()
  if allowFolder and answer.lower() == "n":
    return "n"
  try:
    number = int(answer)
  except ValueError as error:
    if allowName and answer:
      return answer
    raise ToolError(f"enter a number between 1 and {len(items)}") from error
  if number < 1 or number > len(items):
    raise ToolError(f"number must be between 1 and {len(items)}")
  return items[number - 1]


def setPath(session, role):
  if role == "source":
    text = promptPath(f"Source path [{session.pathSource or '<empty>'}]: ")
    if text:
      session.setSourcePath(text)
  else:
    text = promptPath(f"Dest path [{session.pathDestination or '<empty>'}]: ")
    if text:
      session.setDestinationPath(text)


def selectDisk(session, role):
  files = session.listPathFiles(role)
  folderLabel = "Copy File from Folder" if role == "source" else "Extract File to Folder"
  imagePath = chooseNumber(files, "Select Disk: ", allowFolder=True, folderLabel=folderLabel)
  if imagePath == "n":
    if role == "source":
      session.setFolderSource()
    else:
      session.setFolderDestination()
    return
  if imagePath is None:
    return
  if role == "source":
    session.setSource(str(imagePath))
  else:
    session.setDestination(str(imagePath))


def copyFromSource(session):
  source = session.requireSource()
  dest = session.requireDestination(forCopy=True)
  entries = [entry for entry in source.listFiles() if not entry.isDir]
  selected = chooseNumber(entries, "Select file number or enter a file name/wildcard: ", allowName=True)
  if selected is None:
    return
  if isinstance(selected, Entry):
    nameIn = selected.path
  else:
    nameIn = selected
  nameOut = input("Destination name [same as source]: ").strip() or None
  copyFile(source, dest, nameIn, nameOut)


def getOneFileFromSource(session):
  source = session.requireSource()
  targetPath = session.pathDestination
  if targetPath is None:
    raise ToolError("dest path is empty; select it with menu option 3")
  entries = [entry for entry in source.listFiles() if not entry.isDir]
  selected = chooseNumber(entries, "Select file number or enter a file name: ", allowName=True)
  if selected is None:
    return
  entry = selected if isinstance(selected, Entry) else findEntry(source, selected)
  nameOut = input(f"Name in destination [{Path(entry.path).name}]: ").strip() or Path(entry.path).name
  exported = source.exportFile(entry)
  if isinstance(source, PascalVolume) and exported.pascalKind == 3:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ucsd"))
    import extractUcsd
    exported.data = extractUcsd.ucsdTextToHost(exported.data)
  target = FolderDestination(targetPath).importFile(nameOut, exported)
  print(f"extracted '{entry.path}' -> '{target}' ({len(exported.data)} bytes)")


def removeFromDestination(session):
  dest = session.requireDestination()
  entries = [entry for entry in dest.listFiles() if not entry.isDir]
  selected = chooseNumber(entries, "Select file number or enter a file name/wildcard: ", allowName=True)
  if selected is None:
    return
  name = selected.path if isinstance(selected, Entry) else selected
  removeFiles(dest, name)


DISK_IMAGE_SUFFIXES = (".dsk", ".do", ".po", ".2mg", ".2img", ".nib", ".woz", ".hdv", ".img", ".imd", ".td0")


def addHostFile(session):
  dest = session.requireDestination()
  label = "Full path of file to add: "
  baseDirectory = ""
  if session.ucsdMode and session.pathDestination is not None:
    label = f"Full path of file to add [Dest. Path: {session.pathDestination}]: "
    baseDirectory = str(session.pathDestination)
  if session.ucsdMode and session.pathSource is not None and session.source is None:
    #-- no source disk: pick from the loose files in Source path (or type a path)
    chosen = chooseNumber(session.listPathFiles("source"), "Select file number (or type a full path): ", allowName=True)
    if chosen is None:
      return
    text = str(session.pathSource / chosen) if isinstance(chosen, Path) else chosen
    if not isinstance(chosen, Path) and not text.startswith(("/", "~")):
      #-- a wildcard without a directory refers to the files listed from Source path
      wildcardBase = str(session.pathSource) if any(c in text for c in "*?[") else baseDirectory
      if wildcardBase:
        text = os.path.join(wildcardBase, text)
  else:
    text = promptPath(label, baseDirectory)
  hostPath = Path(text).expanduser()
  if any(c in hostPath.name for c in "*?["):
    addHostFiles(dest, hostPath)
    return
  if not hostPath.is_file():
    raise ToolError(f"file not found: {hostPath}")
  nameOut = input(f"Name on disk [{hostPath.name}]: ").strip() or hostPath.name
  stored, size = addHostFileToDestination(dest, hostPath, nameOut)
  dest.image.save()
  print(f"added '{hostPath}' -> '{stored}' ({size} bytes)")


def addHostFileToDestination(dest, hostPath, nameOut):
  data = hostPath.read_bytes()
  suffix = hostPath.suffix.upper()
  pascalKind = {".TEXT": 3, ".CODE": 2, ".PASCAL": 2}.get(suffix, 5)
  if isinstance(dest, PascalVolume) and suffix == ".TEXT":
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ucsd"))
    import extractUcsd
    data = extractUcsd.hostTextToUcsd(data)
  prodosType = 0x04 if suffix in (".TXT", ".TEXT") else 0x06
  stored = dest.importFile(nameOut, Exported(data, prodosType, 0, pascalKind))
  return stored, len(data)


def addHostFiles(dest, patternPath):
  directory = patternPath.parent
  pattern = patternPath.name.upper()
  try:
    candidates = sorted((path for path in directory.iterdir() if path.is_file()),
                        key=lambda path: path.name.casefold())
  except OSError as error:
    raise ToolError(f"cannot list '{directory}': {error}") from error
  matches = [path for path in candidates if fnmatch.fnmatchcase(path.name.upper(), pattern)]
  if not matches:
    raise ToolError(f"no files match '{patternPath}'")
  destinationImage = dest.image.path
  added = 0
  considered = 0
  for path in matches:
    #-- disk images, hidden files and the destination image itself are never added
    if path.suffix.lower() in DISK_IMAGE_SUFFIXES or path.name.startswith("."):
      continue
    if path.resolve() == destinationImage.resolve():
      continue
    considered += 1
    try:
      stored, size = addHostFileToDestination(dest, path, path.name)
      print(f"added '{path}' -> '{stored}' ({size} bytes)")
      added += 1
    except ToolError as error:
      print(f"skipped '{path.name}': {error}")
  if added:
    dest.image.save()
  print(f"{added} of {considered} files added")


def showVolumeFiles(volume):
  entries = [entry for entry in volume.listFiles() if not entry.isDir]
  if not entries:
    print("  (no files)")
    return
  for number, entry in enumerate(entries, 1):
    print(f"{number}. {entry.path}")


def listFilesOnSource(session):
  folderMode = session.copyFromFolder
  if session.pathSource is None or folderMode:
    showVolumeFiles(session.requireSource())
    return
  if session.source is not None:
    print(f"Files on disk {session.sourceImage.path.name}:")
    showVolumeFiles(session.source)
    print()
  print(f"Files in Source path {session.pathSource}:")
  showVolumeFiles(FolderSource(session.pathSource))


def listFilesOnDestination(session):
  showVolumeFiles(session.requireDestination())


def showMenu(session):
  print()
  session.showSourcePath()
  session.showSource()
  session.showDestinationPath()
  session.showDestination()
  print("5 - List files on Source (L)")
  print("6 - List files on Destination")
  print("7 - Get one specific file From Source")
  print("8 - Copy file from Source to Destination (C)")
  print("9 - Remove file from Destination")
  print("10 - Add one specific file (full path, or wildcard such as *) to Destination (A)")
  print("Q - Quit")


def main(argv, ucsdMode=False, opener=None):
  if len(argv) != 1:
    print(__doc__)
    return 1
  setupLineEditing()
  session = Session(ucsdMode, opener)
  while True:
    showMenu(session)
    try:
      option = input("Select option: ").strip().upper()
    except EOFError:
      print()
      return 0
    try:
      if option in ("0", "Q"):
        return 0
      elif option == "1":
        setPath(session, "source")
      elif option == "2":
        selectDisk(session, "source")
      elif option == "3":
        setPath(session, "destination")
      elif option == "4":
        selectDisk(session, "destination")
      elif option in ("5", "L"):
        listFilesOnSource(session)
      elif option == "6":
        listFilesOnDestination(session)
      elif option == "7":
        getOneFileFromSource(session)
      elif option in ("8", "C"):
        copyFromSource(session)
      elif option == "9":
        removeFromDestination(session)
      elif option in ("10", "A"):
        addHostFile(session)
      else:
        print("Select an option from 1 to 10, or L, C, A, or Q.")
    except (ToolError, ValueError) as error:
      print(f"error: {error}")
    except EOFError:
      print()
      return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
