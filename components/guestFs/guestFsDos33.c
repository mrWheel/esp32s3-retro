#include "guestFs.h"
#include "guestFsInternal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-- DOS 3.3 adapter for 140K Apple II images (either sector order, handled by guestFsApple.c).
//-- Reference for the read side: tools/apple2/extractApple2Files.py Dos33Volume. The write side has
//no Python
//-- counterpart; tests/guestFsDos33Fixture.py carries an independent writer and checker.
//--
//-- Layout: VTOC at track 17 sector 0 (catalog start +1/+2, bitmap at +0x38, 4 bytes per track:
// byte 0 holds
//-- sectors 15..8, byte 1 holds sectors 7..0, bit 7 = highest sector, 1 = free). Catalog sectors
// hold seven
//-- 35-byte entries at +0x0B: track/sector of the first T/S list, file type (bit 7 = locked), 30
// name bytes with
//-- the high bit set and padded with 0xA0, and the sector count (little endian). A T/S list holds
// 122 pairs
//-- from +0x0C, the next list at +1/+2, the file position of its first sector at +5/+6 (written,
// but the
//-- reader like the Python tool simply concatenates 122 pairs per list).
//--
//-- Writing: free sectors are searched on tracks 1..34 (track 17 skipped, it is the catalog track)
// with
//-- sectors 15..0 inside a track, and finally on track 0. Each T/S list sector is allocated right
// before the
//-- (up to) 122 data sectors it describes. Names for NEW files: 1..30 characters, upper case,
// 0x20..0x5F, no
//-- comma, first character a letter, last character not a space. Existing names are matched
// case-insensitively
//-- on the name with trailing blanks removed.

_Static_assert(guestFsNameCapacity >= 31, "DOS 3.3 names need 30 characters plus a terminator");

enum
{
  trackCount = 35,
  sectorsPerTrack = 16,
  sectorTotal = trackCount * sectorsPerTrack,
  sectorSize = guestFsAppleSectorSize,
  vtocTrack = 17,
  vtocSector = 0,
  bitmapOffset = 0x38,
  catalogEntryOffset = 0x0B,
  catalogEntrySize = 35,
  catalogEntriesPerSector = 7,
  nameBytes = 30,
  nameOffset = 3,
  pairsPerList = 122,
  listPairOffset = 0x0C,
  deletedMarker = 0xFF,
  lockedBit = 0x80,
  typeText = 0x00,
  typeInteger = 0x01,
  typeApplesoft = 0x02,
  typeBinary = 0x04,
  typeRelocatable = 0x10,
  maxHeaderedLength = 0xFFFF
};

typedef struct
{
  guestFsVolume base;
  guestFsAppleDevice device;
  bool needsReopen;
  char journalPath[320];
  char journalId[guestFsJournalIdCapacity];
} dosVolume;

typedef struct
{
  uint8_t track;
  uint8_t sector;
  uint8_t slot;
  uint8_t bytes[catalogEntrySize];
} catalogSlot;

typedef struct
{
  char name[guestFsNameCapacity];
  uint8_t rawType;
  bool locked;
  uint8_t listTrack;
  uint8_t listSector;
  uint16_t catalogSectors;
  bool damaged;
  uint32_t rawSectors;
  uint32_t dataSectors;
  uint32_t listSectors;
  uint32_t headerBytes;
  uint32_t size;
  uint16_t address;
} fileInfo;

typedef struct
{
  uint8_t vtoc[sectorSize];
  uint8_t claims[sectorTotal];
} dosState;

static uint16_t le16(const uint8_t* bytes)
{
  return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static bool sectorInRange(uint32_t track, uint32_t sector)
{
  return track < trackCount && sector < sectorsPerTrack;
}

static uint32_t sectorIndex(uint32_t track, uint32_t sector)
{
  return track * sectorsPerTrack + sector;
}

static bool readSector(const dosVolume* volume, uint32_t track, uint32_t sector, uint8_t* buffer)
{
  return guestFsAppleReadDosSector(&volume->device, track, sector, buffer);
}

static bool writeSector(const dosVolume* volume, uint32_t track, uint32_t sector,
                        const uint8_t* buffer)
{
  return guestFsAppleWriteDosSector(&volume->device, track, sector, buffer);
}

static void setDetail(char* detail, size_t capacity, const char* text)
{
  if (detail != NULL && capacity > 0)
  {
    snprintf(detail, capacity, "%s", text);
  }
}

//-- Same conditions as Dos33Volume.probe in tools/apple2/extractApple2Files.py.
static bool vtocLooksValid(const uint8_t* vtoc)
{
  return vtoc[3] >= 1 && vtoc[3] <= 3 && vtoc[0x27] == 122 && vtoc[0x34] == 35 &&
         vtoc[0x35] == 16 && vtoc[1] >= 1 && vtoc[1] <= 34 && vtoc[2] <= 15;
}

static bool sectorFree(const uint8_t* vtoc, uint32_t track, uint32_t sector)
{
  return (vtoc[bitmapOffset + 4 * track + (sector < 8 ? 1 : 0)] & (1u << (sector & 7))) != 0;
}

static void setSectorFree(uint8_t* vtoc, uint32_t track, uint32_t sector, bool freeFlag)
{
  uint8_t* byte = &vtoc[bitmapOffset + 4 * track + (sector < 8 ? 1 : 0)];
  uint8_t mask = (uint8_t)(1u << (sector & 7));
  *byte = freeFlag ? (uint8_t)(*byte | mask) : (uint8_t)(*byte & ~mask);
}

//-- A (0,0) track/sector pair marks a hole in a list, so track 0 sector 0 can never hold file data.
static bool sectorUsable(const uint8_t* vtoc, uint32_t track, uint32_t sector)
{
  return (track != 0 || sector != 0) && sectorFree(vtoc, track, sector);
}

static uint32_t countFreeSectors(const uint8_t* vtoc)
{
  uint32_t count = 0;
  for (uint32_t track = 0; track < trackCount; ++track)
  {
    for (uint32_t sector = 0; sector < sectorsPerTrack; ++sector)
    {
      count += sectorUsable(vtoc, track, sector) ? 1 : 0;
    }
  }
  return count;
}

//-- Free sectors in allocation order: tracks 1..34 without 17, sectors 15..0, then track 0.
static uint32_t collectFreeSectors(const uint8_t* vtoc, uint32_t wanted, uint8_t (*out)[2])
{
  uint32_t found = 0;
  for (uint32_t step = 0; step < trackCount && found < wanted; ++step)
  {
    uint32_t track = step == trackCount - 1 ? 0 : step + 1;
    if (track == vtocTrack)
    {
      continue;
    }
    for (int sector = sectorsPerTrack - 1; sector >= 0 && found < wanted; --sector)
    {
      if (sectorUsable(vtoc, track, (uint32_t)sector))
      {
        out[found][0] = (uint8_t)track;
        out[found][1] = (uint8_t)sector;
        ++found;
      }
    }
  }
  return found;
}

static bool pythonWhitespace(uint8_t character)
{
  return (character >= 9 && character <= 13) || (character >= 28 && character <= 32);
}

//-- Mirrors the reader: 7-bit characters, trailing whitespace removed; control characters shown as
//'?'.
static void decodeName(const uint8_t* entry, char name[guestFsNameCapacity])
{
  size_t end = nameBytes;
  while (end > 0 && pythonWhitespace(entry[nameOffset + end - 1] & 0x7F))
  {
    --end;
  }
  for (size_t index = 0; index < end; ++index)
  {
    uint8_t character = entry[nameOffset + index] & 0x7F;
    name[index] = (character < 0x20 || character == 0x7F) ? '?' : (char)character;
  }
  name[end] = '\0';
}

static char upperCase(char character)
{
  return (character >= 'a' && character <= 'z') ? (char)(character - 'a' + 'A') : character;
}

static bool sameName(const char* first, const char* second)
{
  while (*first != '\0' && *second != '\0' && upperCase(*first) == upperCase(*second))
  {
    ++first;
    ++second;
  }
  return *first == '\0' && *second == '\0';
}

static bool lookupName(const char* name, char canonical[guestFsNameCapacity])
{
  size_t length = name == NULL ? 0 : strlen(name);
  if (length == 0 || length > nameBytes)
  {
    return false;
  }
  for (size_t index = 0; index < length; ++index)
  {
    canonical[index] = upperCase(name[index]);
  }
  canonical[length] = '\0';
  return true;
}

static bool newName(const char* name, char canonical[guestFsNameCapacity])
{
  if (!lookupName(name, canonical))
  {
    return false;
  }
  size_t length = strlen(canonical);
  if (canonical[0] < 'A' || canonical[0] > 'Z' || canonical[length - 1] == ' ')
  {
    return false;
  }
  for (size_t index = 0; index < length; ++index)
  {
    char character = canonical[index];
    if (character < 0x20 || character > 0x5F || character == ',')
    {
      return false;
    }
  }
  return true;
}

static void encodeName(const char* canonical, uint8_t* target)
{
  size_t length = strlen(canonical);
  for (size_t index = 0; index < nameBytes; ++index)
  {
    target[index] = index < length ? (uint8_t)((uint8_t)canonical[index] | 0x80) : 0xA0;
  }
}

//-- Visits every catalog sector once (loop and range checked) and each of its seven slots.
//-- The visitor returns false to stop. Corrupt: loop, out-of-range link.
typedef bool (*catalogVisitFn)(void* context, const catalogSlot* slot);

static guestFsStatus walkCatalog(const dosVolume* volume, const uint8_t* vtoc, catalogVisitFn visit,
                                 void* context)
{
  uint8_t seen[sectorTotal / 8 + 1];
  uint8_t buffer[sectorSize];
  uint8_t track = vtoc[1];
  uint8_t sector = vtoc[2];
  memset(seen, 0, sizeof(seen));
  while (track != 0)
  {
    uint32_t index = sectorIndex(track, sector);
    if (!sectorInRange(track, sector) || (seen[index / 8] & (1u << (index % 8))) != 0)
    {
      return guestFsErrCorrupt;
    }
    seen[index / 8] = (uint8_t)(seen[index / 8] | (1u << (index % 8)));
    if (!readSector(volume, track, sector, buffer))
    {
      return guestFsErrIo;
    }
    for (uint8_t slotNumber = 0; slotNumber < catalogEntriesPerSector; ++slotNumber)
    {
      catalogSlot slot;
      slot.track = track;
      slot.sector = sector;
      slot.slot = slotNumber;
      memcpy(slot.bytes, buffer + catalogEntryOffset + catalogEntrySize * slotNumber,
             catalogEntrySize);
      if (!visit(context, &slot))
      {
        return guestFsOk;
      }
    }
    track = buffer[1];
    sector = buffer[2];
  }
  return guestFsOk;
}

static bool slotInUse(const catalogSlot* slot)
{
  return slot->bytes[0] != 0 && slot->bytes[0] != deletedMarker;
}

//-- Visits the T/S list sectors (isList) and every non-sparse data sector (dataIndex = position in
// the file).
//-- Returns guestFsErrDamagedFile for loops and out-of-range pointers, guestFsErrIo for read
// errors.
typedef void (*fileVisitFn)(void* context, uint32_t track, uint32_t sector, bool isList,
                            uint32_t dataIndex);

static guestFsStatus visitFile(const dosVolume* volume, uint32_t listTrack, uint32_t listSector,
                               fileVisitFn visit, void* context)
{
  uint8_t seen[sectorTotal / 8 + 1];
  uint8_t list[sectorSize];
  uint32_t track = listTrack;
  uint32_t sector = listSector;
  uint32_t listNumber = 0;
  memset(seen, 0, sizeof(seen));
  while (track != 0)
  {
    uint32_t index = sectorIndex(track, sector);
    if (!sectorInRange(track, sector) || (seen[index / 8] & (1u << (index % 8))) != 0)
    {
      return guestFsErrDamagedFile;
    }
    seen[index / 8] = (uint8_t)(seen[index / 8] | (1u << (index % 8)));
    if (!readSector(volume, track, sector, list))
    {
      return guestFsErrIo;
    }
    visit(context, track, sector, true, 0);
    for (uint32_t pair = 0; pair < pairsPerList; ++pair)
    {
      uint8_t dataTrack = list[listPairOffset + 2 * pair];
      uint8_t dataSector = list[listPairOffset + 2 * pair + 1];
      if (dataTrack == 0 && dataSector == 0)
      {
        continue;
      }
      if (!sectorInRange(dataTrack, dataSector))
      {
        return guestFsErrDamagedFile;
      }
      visit(context, dataTrack, dataSector, false, listNumber * pairsPerList + pair);
    }
    ++listNumber;
    track = list[1];
    sector = list[2];
  }
  return guestFsOk;
}

typedef struct
{
  uint32_t listCount;
  uint32_t dataCount;
  uint32_t rawSectors;
} totals;

static void totalsVisit(void* context, uint32_t track, uint32_t sector, bool isList,
                        uint32_t dataIndex)
{
  totals* counts = context;
  (void)track;
  (void)sector;
  if (isList)
  {
    ++counts->listCount;
    return;
  }
  ++counts->dataCount;
  if (dataIndex + 1 > counts->rawSectors)
  {
    counts->rawSectors = dataIndex + 1;
  }
}

//-- Random access to the sector of a file by position; remembers the T/S list it is working in.
typedef struct
{
  const dosVolume* volume;
  uint8_t firstTrack;
  uint8_t firstSector;
  bool loaded;
  uint32_t listNumber;
  uint8_t buffer[sectorSize];
} cursor;

static void cursorStart(cursor* position, const dosVolume* volume, uint8_t track, uint8_t sector)
{
  memset(position, 0, sizeof(*position));
  position->volume = volume;
  position->firstTrack = track;
  position->firstSector = sector;
}

//-- 1 = sector found, 0 = sparse (reads as zeros), -1 = I/O or structure error.
static int cursorLookup(cursor* position, uint32_t fileSector, uint8_t* track, uint8_t* sector)
{
  uint32_t wanted = fileSector / pairsPerList;
  if (!position->loaded || wanted < position->listNumber)
  {
    if (!sectorInRange(position->firstTrack, position->firstSector) ||
        !readSector(position->volume, position->firstTrack, position->firstSector,
                    position->buffer))
    {
      return -1;
    }
    position->loaded = true;
    position->listNumber = 0;
  }
  while (position->listNumber < wanted)
  {
    uint8_t nextTrack = position->buffer[1];
    uint8_t nextSector = position->buffer[2];
    if (nextTrack == 0)
    {
      return 0;
    }
    if (!sectorInRange(nextTrack, nextSector) ||
        !readSector(position->volume, nextTrack, nextSector, position->buffer))
    {
      return -1;
    }
    ++position->listNumber;
  }
  uint32_t pair = fileSector % pairsPerList;
  uint8_t dataTrack = position->buffer[listPairOffset + 2 * pair];
  uint8_t dataSector = position->buffer[listPairOffset + 2 * pair + 1];
  if (dataTrack == 0 && dataSector == 0)
  {
    return 0;
  }
  *track = dataTrack;
  *sector = dataSector;
  return 1;
}

static uint32_t headerBytesFor(uint8_t rawType)
{
  switch (rawType)
  {
  case typeInteger:
  case typeApplesoft:
    return 2;
  case typeBinary:
    return 4;
  default:
    return 0;
  }
}

//-- Same file semantics as Dos33Volume.exportFile: headers stripped, text trimmed of trailing NULs.
static guestFsStatus describeFile(const dosVolume* volume, const uint8_t* entry, fileInfo* info)
{
  totals counts = {0, 0, 0};
  memset(info, 0, sizeof(*info));
  decodeName(entry, info->name);
  info->rawType = entry[2] & 0x7F;
  info->locked = (entry[2] & lockedBit) != 0;
  info->listTrack = entry[0];
  info->listSector = entry[1];
  info->catalogSectors = le16(entry + 33);
  info->size = (uint32_t)info->catalogSectors * sectorSize;
  info->headerBytes = headerBytesFor(info->rawType);
  guestFsStatus status = visitFile(volume, entry[0], entry[1], totalsVisit, &counts);
  if (status == guestFsErrIo)
  {
    return guestFsErrIo;
  }
  if (status != guestFsOk)
  {
    info->damaged = true;
    return guestFsOk;
  }
  info->listSectors = counts.listCount;
  info->dataSectors = counts.dataCount;
  info->rawSectors = counts.rawSectors;
  uint32_t rawLength = info->rawSectors * sectorSize;
  cursor position;
  cursorStart(&position, volume, entry[0], entry[1]);
  if (info->headerBytes > 0)
  {
    uint8_t track;
    uint8_t sector;
    uint8_t first[sectorSize];
    if (rawLength < info->headerBytes)
    {
      info->damaged = true;
      return guestFsOk;
    }
    int found = cursorLookup(&position, 0, &track, &sector);
    if (found < 0 || (found > 0 && !readSector(volume, track, sector, first)))
    {
      return guestFsErrIo;
    }
    if (found == 0)
    {
      memset(first, 0, sizeof(first));
    }
    uint32_t length = info->headerBytes == 2 ? le16(first) : le16(first + 2);
    info->address = info->headerBytes == 4 ? le16(first) : 0;
    info->size = rawLength - info->headerBytes < length ? rawLength - info->headerBytes : length;
    return guestFsOk;
  }
  if (info->rawType == typeText)
  {
    info->size = 0;
    for (uint32_t index = info->rawSectors; index > 0; --index)
    {
      uint8_t track;
      uint8_t sector;
      uint8_t data[sectorSize];
      int found = cursorLookup(&position, index - 1, &track, &sector);
      if (found < 0 || (found > 0 && !readSector(volume, track, sector, data)))
      {
        return guestFsErrIo;
      }
      if (found == 0)
      {
        continue;
      }
      int last = sectorSize - 1;
      while (last >= 0 && data[last] == 0)
      {
        --last;
      }
      if (last >= 0)
      {
        info->size = (index - 1) * sectorSize + (uint32_t)last + 1;
        break;
      }
    }
    return guestFsOk;
  }
  info->size = rawLength;
  return guestFsOk;
}

static void typeLabelFor(uint8_t rawType, char label[guestFsTypeLabelCapacity])
{
  switch (rawType)
  {
  case 0x00:
    snprintf(label, guestFsTypeLabelCapacity, "T");
    break;
  case 0x01:
    snprintf(label, guestFsTypeLabelCapacity, "I");
    break;
  case 0x02:
    snprintf(label, guestFsTypeLabelCapacity, "A");
    break;
  case 0x04:
    snprintf(label, guestFsTypeLabelCapacity, "B");
    break;
  case 0x08:
    snprintf(label, guestFsTypeLabelCapacity, "S");
    break;
  case 0x10:
    snprintf(label, guestFsTypeLabelCapacity, "R");
    break;
  case 0x20:
    snprintf(label, guestFsTypeLabelCapacity, "A2");
    break;
  case 0x40:
    snprintf(label, guestFsTypeLabelCapacity, "B2");
    break;
  default:
    snprintf(label, guestFsTypeLabelCapacity, "$%02X", rawType);
    break;
  }
}

//-- Interchange attributes exactly as Dos33Volume.exportFile reports them.
static void fillAttributes(const fileInfo* info, guestFsAttributes* attributes)
{
  memset(attributes, 0, sizeof(*attributes));
  attributes->pascalKind = 5;
  attributes->locked = info->locked;
  switch (info->rawType)
  {
  case typeText:
    attributes->prodosType = 0x04;
    break;
  case typeInteger:
    attributes->prodosType = 0xFA;
    attributes->auxType = 0x0800;
    break;
  case typeApplesoft:
    attributes->prodosType = 0xFC;
    attributes->auxType = 0x0801;
    break;
  case typeBinary:
    attributes->prodosType = 0x06;
    attributes->auxType = info->address;
    break;
  case typeRelocatable:
    attributes->prodosType = 0xFE;
    break;
  default:
    attributes->prodosType = 0x06;
    break;
  }
}

static void fillEntry(const fileInfo* info, guestFsEntry* entry)
{
  memset(entry, 0, sizeof(*entry));
  snprintf(entry->name, sizeof(entry->name), "%s", info->name);
  entry->size = info->size;
  entry->allocatedBytes = (uint32_t)info->catalogSectors * sectorSize;
  entry->readOnlyAttribute = info->locked;
  entry->damaged = info->damaged;
  typeLabelFor(info->rawType, entry->typeLabel);
  if (!info->damaged)
  {
    entry->hasAttributes = true;
    fillAttributes(info, &entry->attributes);
  }
}

static guestFsStatus readVtoc(const dosVolume* volume, uint8_t* vtoc)
{
  if (!readSector(volume, vtocTrack, vtocSector, vtoc))
  {
    return guestFsErrIo;
  }
  return vtocLooksValid(vtoc) ? guestFsOk : guestFsErrCorrupt;
}

typedef struct
{
  const char* wanted;
  uint32_t matches;
  catalogSlot match;
  bool haveFree;
  catalogSlot firstFree;
  uint32_t slots;
  uint32_t freeSlots;
} findContext;

static bool findVisit(void* context, const catalogSlot* slot)
{
  findContext* found = context;
  ++found->slots;
  if (!slotInUse(slot))
  {
    ++found->freeSlots;
    if (!found->haveFree)
    {
      found->haveFree = true;
      found->firstFree = *slot;
    }
    return true;
  }
  if (found->wanted != NULL)
  {
    char name[guestFsNameCapacity];
    decodeName(slot->bytes, name);
    if (sameName(name, found->wanted))
    {
      if (found->matches == 0)
      {
        found->match = *slot;
      }
      ++found->matches;
    }
  }
  return true;
}

//-- Reads the VTOC and scans the catalog for `wanted` (may be NULL) and for the first free slot.
static guestFsStatus findInCatalog(const dosVolume* volume, uint8_t* vtoc, const char* wanted,
                                   findContext* found)
{
  memset(found, 0, sizeof(*found));
  found->wanted = wanted;
  guestFsStatus status = readVtoc(volume, vtoc);
  if (status != guestFsOk)
  {
    return status;
  }
  return walkCatalog(volume, vtoc, findVisit, found);
}

static bool noopVisit(void* context, const catalogSlot* slot)
{
  (void)context;
  (void)slot;
  return true;
}

static bool dosSupportsOs(const char* osId)
{
  return guestFsAppleSupportsOs(osId);
}

static void dosClose(guestFsVolume* base)
{
  free(base);
}

static guestFsStatus dosOpen(const guestFsIo* io, const char* osId, const char* journalPath,
                             const char* journalId, guestFsVolume** result, char* detail,
                             size_t detailCapacity)
{
  (void)osId;
  if (strlen(journalPath) >= sizeof(((dosVolume*)0)->journalPath) ||
      strlen(journalId) >= guestFsJournalIdCapacity)
  {
    return guestFsErrInvalidArgument;
  }
  dosVolume* volume = calloc(1, sizeof(*volume));
  if (volume == NULL)
  {
    return guestFsErrIo;
  }
  volume->base.ops = &guestFsDos33Ops;
  strcpy(volume->journalPath, journalPath);
  strcpy(volume->journalId, journalId);
  guestFsStatus status =
      guestFsAppleOpenDevice(&volume->device, io, journalId, detail, detailCapacity);
  if (status != guestFsOk)
  {
    free(volume);
    return status;
  }
  uint8_t vtoc[sectorSize];
  if (io->size != guestFsAppleDosImageSize)
  {
    setDetail(detail, detailCapacity, "Not a recognised ProDOS, UCSD Pascal or DOS 3.3 volume");
    free(volume);
    return guestFsErrUnsupported;
  }
  if (!readSector(volume, vtocTrack, vtocSector, vtoc))
  {
    free(volume);
    return guestFsErrIo;
  }
  if (!vtocLooksValid(vtoc))
  {
    setDetail(detail, detailCapacity, "Not a recognised ProDOS, UCSD Pascal or DOS 3.3 volume");
    free(volume);
    return guestFsErrUnsupported;
  }
  status = walkCatalog(volume, vtoc, noopVisit, NULL);
  if (status != guestFsOk)
  {
    setDetail(detail, detailCapacity,
              status == guestFsErrCorrupt ? "DOS 3.3 catalog chain is damaged"
                                          : "DOS 3.3 catalog could not be read");
    free(volume);
    return status;
  }
  *result = &volume->base;
  return guestFsOk;
}

static void dosInfo(const guestFsVolume* base, guestFsInfo* info)
{
  const dosVolume* volume = (const dosVolume*)base;
  uint8_t vtoc[sectorSize];
  findContext found;
  memset(info, 0, sizeof(*info));
  info->fsName = "DOS 3.3";
  info->profileName = "140K";
  info->blockSize = sectorSize;
  info->totalBlocks = sectorTotal;
  info->maxUser = 0;
  info->nameRule = "dos33";
  if (findInCatalog(volume, vtoc, NULL, &found) == guestFsOk)
  {
    info->freeBlocks = countFreeSectors(vtoc);
    info->directoryEntries = found.slots;
    info->freeDirectoryEntries = found.freeSlots;
  }
}

typedef struct
{
  const dosVolume* volume;
  guestFsListFn callback;
  void* callbackContext;
  guestFsStatus status;
} listContext;

static bool listVisit(void* context, const catalogSlot* slot)
{
  listContext* list = context;
  fileInfo info;
  guestFsEntry entry;
  if (!slotInUse(slot))
  {
    return true;
  }
  list->status = describeFile(list->volume, slot->bytes, &info);
  if (list->status != guestFsOk)
  {
    return false;
  }
  fillEntry(&info, &entry);
  return list->callback(list->callbackContext, &entry);
}

static guestFsStatus dosList(guestFsVolume* base, guestFsListFn callback, void* context)
{
  const dosVolume* volume = (const dosVolume*)base;
  uint8_t vtoc[sectorSize];
  listContext list = {volume, callback, context, guestFsOk};
  guestFsStatus status = readVtoc(volume, vtoc);
  if (status != guestFsOk)
  {
    return status;
  }
  status = walkCatalog(volume, vtoc, listVisit, &list);
  return status != guestFsOk ? status : list.status;
}

//-- Looks a file up by name; on success *info and *slot describe the first match.
static guestFsStatus locateFile(const dosVolume* volume, uint8_t user, const char* name,
                                fileInfo* info, catalogSlot* slot)
{
  char wanted[guestFsNameCapacity];
  uint8_t vtoc[sectorSize];
  findContext found;
  if (user != 0)
  {
    return guestFsErrInvalidArgument;
  }
  if (!lookupName(name, wanted))
  {
    return guestFsErrBadName;
  }
  guestFsStatus status = findInCatalog(volume, vtoc, wanted, &found);
  if (status != guestFsOk)
  {
    return status;
  }
  if (found.matches == 0)
  {
    return guestFsErrNotFound;
  }
  if (slot != NULL)
  {
    *slot = found.match;
  }
  return describeFile(volume, found.match.bytes, info);
}

static guestFsStatus dosStat(guestFsVolume* base, uint8_t user, const char* name,
                             guestFsEntry* entry)
{
  fileInfo info;
  guestFsStatus status = locateFile((const dosVolume*)base, user, name, &info, NULL);
  if (status == guestFsOk)
  {
    fillEntry(&info, entry);
  }
  return status;
}

static guestFsStatus dosReadAt(guestFsVolume* base, uint8_t user, const char* name, uint32_t offset,
                               void* buffer, size_t length, size_t* got)
{
  const dosVolume* volume = (const dosVolume*)base;
  fileInfo info;
  uint8_t* target = buffer;
  *got = 0;
  guestFsStatus status = locateFile(volume, user, name, &info, NULL);
  if (status != guestFsOk)
  {
    return status;
  }
  if (info.damaged)
  {
    return guestFsErrDamagedFile;
  }
  if (offset >= info.size)
  {
    return guestFsOk;
  }
  if (length > info.size - offset)
  {
    length = info.size - offset;
  }
  cursor position;
  cursorStart(&position, volume, info.listTrack, info.listSector);
  uint32_t rawPosition = offset + info.headerBytes;
  while (length > 0)
  {
    uint8_t track;
    uint8_t sector;
    uint8_t data[sectorSize];
    uint32_t inside = rawPosition % sectorSize;
    size_t count = sectorSize - inside;
    count = count > length ? length : count;
    int found = cursorLookup(&position, rawPosition / sectorSize, &track, &sector);
    if (found < 0 || (found > 0 && !readSector(volume, track, sector, data)))
    {
      return guestFsErrIo;
    }
    if (found == 0)
    {
      memset(data, 0, sizeof(data));
    }
    memcpy(target, data + inside, count);
    target += count;
    length -= count;
    rawPosition += (uint32_t)count;
    *got += count;
  }
  return guestFsOk;
}

//-- Mutation prerequisites: the VTOC is sane and every sector is claimed at most once, and every
// claimed
//-- sector is marked used in the bitmap. Anything else is guestFsErrCorrupt (fail closed).
typedef struct
{
  const dosVolume* volume;
  dosState* state;
  bool shared;
  guestFsStatus status;
} loadContext;

static void claimSector(loadContext* load, uint32_t track, uint32_t sector)
{
  uint8_t* claim = &load->state->claims[sectorIndex(track, sector)];
  if (*claim != 0)
  {
    load->shared = true;
  }
  *claim = 1;
}

static void claimVisit(void* context, uint32_t track, uint32_t sector, bool isList,
                       uint32_t dataIndex)
{
  (void)isList;
  (void)dataIndex;
  claimSector(context, track, sector);
}

static bool loadVisit(void* context, const catalogSlot* slot)
{
  loadContext* load = context;
  if (slot->slot == 0)
  {
    claimSector(load, slot->track, slot->sector);
  }
  if (!slotInUse(slot))
  {
    return !load->shared;
  }
  guestFsStatus status = visitFile(load->volume, slot->bytes[0], slot->bytes[1], claimVisit, load);
  if (status == guestFsErrIo)
  {
    load->status = guestFsErrIo;
    return false;
  }
  return !load->shared;
}

static guestFsStatus loadState(const dosVolume* volume, dosState* state)
{
  loadContext load = {volume, state, false, guestFsOk};
  memset(state, 0, sizeof(*state));
  guestFsStatus status = readVtoc(volume, state->vtoc);
  if (status != guestFsOk)
  {
    return status;
  }
  if (state->vtoc[0x36] != 0 || state->vtoc[0x37] != 1)
  {
    return guestFsErrCorrupt;
  }
  claimSector(&load, vtocTrack, vtocSector);
  status = walkCatalog(volume, state->vtoc, loadVisit, &load);
  if (status != guestFsOk)
  {
    return status;
  }
  if (load.status != guestFsOk)
  {
    return load.status;
  }
  if (load.shared)
  {
    return guestFsErrCorrupt;
  }
  for (uint32_t track = 0; track < trackCount; ++track)
  {
    for (uint32_t sector = 0; sector < sectorsPerTrack; ++sector)
    {
      if (state->claims[sectorIndex(track, sector)] != 0 && sectorFree(state->vtoc, track, sector))
      {
        return guestFsErrCorrupt;
      }
    }
  }
  return guestFsOk;
}

typedef struct
{
  uint8_t* vtoc;
} releaseContext;

static void releaseVisit(void* context, uint32_t track, uint32_t sector, bool isList,
                         uint32_t dataIndex)
{
  releaseContext* release = context;
  (void)isList;
  (void)dataIndex;
  setSectorFree(release->vtoc, track, sector, true);
}

//-- Commits the VTOC sector and one catalog sector as a single journaled change.
static guestFsStatus commitVtocAndCatalog(dosVolume* volume, const uint8_t* vtoc,
                                          const catalogSlot* slot, const uint8_t* catalogSector)
{
  guestFsRangeChange changes[2];
  guestFsAppleDosSectorChange(&volume->device, vtocTrack, vtocSector, vtoc, &changes[0]);
  guestFsAppleDosSectorChange(&volume->device, slot->track, slot->sector, catalogSector,
                              &changes[1]);
  return guestFsRangeCommit(&volume->device.io, volume->journalPath, volume->journalId, changes, 2,
                            &volume->needsReopen);
}

typedef struct
{
  uint8_t rawType;
  uint32_t headerBytes;
  uint16_t address;
} newFileType;

//-- ProDOS file type / aux type are the interchange currency (see Dos33Volume.exportFile for the
// reverse).
static void chooseType(const guestFsAttributes* attributes, newFileType* type)
{
  type->rawType = typeText;
  type->headerBytes = 0;
  type->address = 0;
  if (attributes == NULL)
  {
    return;
  }
  switch (attributes->prodosType)
  {
  case 0x04:
    break;
  case 0xFA:
    type->rawType = typeInteger;
    type->headerBytes = 2;
    break;
  case 0xFC:
    type->rawType = typeApplesoft;
    type->headerBytes = 2;
    break;
  case 0xFE:
    type->rawType = typeRelocatable;
    break;
  case 0x06:
    type->rawType = typeBinary;
    type->headerBytes = 4;
    type->address = attributes->auxType;
    break;
  default:
    type->rawType = typeBinary;
    type->headerBytes = 4;
    break;
  }
}

static void buildListSector(uint8_t* list, uint32_t listNumber, const uint8_t (*alloc)[2],
                            uint32_t listCount, uint32_t dataSectors)
{
  memset(list, 0, sectorSize);
  if (listNumber + 1 < listCount)
  {
    list[1] = alloc[(listNumber + 1) * (pairsPerList + 1)][0];
    list[2] = alloc[(listNumber + 1) * (pairsPerList + 1)][1];
  }
  list[5] = (uint8_t)((listNumber * pairsPerList) & 0xFF);
  list[6] = (uint8_t)((listNumber * pairsPerList) >> 8);
  for (uint32_t pair = 0; pair < pairsPerList; ++pair)
  {
    uint32_t dataIndex = listNumber * pairsPerList + pair;
    if (dataIndex >= dataSectors)
    {
      break;
    }
    const uint8_t* target = alloc[listNumber * (pairsPerList + 1) + 1 + pair];
    list[listPairOffset + 2 * pair] = target[0];
    list[listPairOffset + 2 * pair + 1] = target[1];
  }
}

//-- Writes the T/S list and data sectors of a new file into the allocated (still unreferenced)
// sectors.
static guestFsStatus writeFileSectors(const dosVolume* volume, const newFileType* type,
                                      uint32_t length, uint8_t (*alloc)[2], uint32_t listCount,
                                      uint32_t dataSectors, guestFsSourceFn source, void* context)
{
  uint8_t sectorBuffer[sectorSize];
  uint64_t payload = (uint64_t)length + type->headerBytes;
  for (uint32_t listNumber = 0; listNumber < listCount; ++listNumber)
  {
    buildListSector(sectorBuffer, listNumber, (const uint8_t (*)[2])alloc, listCount, dataSectors);
    const uint8_t* where = alloc[listNumber * (pairsPerList + 1)];
    if (!writeSector(volume, where[0], where[1], sectorBuffer))
    {
      return guestFsErrIo;
    }
  }
  for (uint32_t dataIndex = 0; dataIndex < dataSectors; ++dataIndex)
  {
    uint64_t start = (uint64_t)dataIndex * sectorSize;
    uint32_t fill = payload - start < sectorSize ? (uint32_t)(payload - start) : sectorSize;
    uint32_t skip = 0;
    memset(sectorBuffer, 0, sizeof(sectorBuffer));
    if (dataIndex == 0 && type->headerBytes > 0)
    {
      if (type->headerBytes == 4)
      {
        sectorBuffer[0] = (uint8_t)(type->address & 0xFF);
        sectorBuffer[1] = (uint8_t)(type->address >> 8);
        sectorBuffer[2] = (uint8_t)(length & 0xFF);
        sectorBuffer[3] = (uint8_t)(length >> 8);
      }
      else
      {
        sectorBuffer[0] = (uint8_t)(length & 0xFF);
        sectorBuffer[1] = (uint8_t)(length >> 8);
      }
      skip = type->headerBytes;
    }
    if (fill > skip && !source(context, sectorBuffer + skip, fill - skip))
    {
      return guestFsErrAborted;
    }
    const uint8_t* where =
        alloc[(dataIndex / pairsPerList) * (pairsPerList + 1) + 1 + dataIndex % pairsPerList];
    if (!writeSector(volume, where[0], where[1], sectorBuffer))
    {
      return guestFsErrIo;
    }
  }
  return guestFsOk;
}

static guestFsStatus dosWrite(guestFsVolume* base, uint8_t user, const char* name, uint32_t length,
                              guestFsSourceFn source, void* context, bool overwrite,
                              const guestFsAttributes* attributes)
{
  dosVolume* volume = (dosVolume*)base;
  char canonical[guestFsNameCapacity];
  newFileType type;
  findContext found;
  dosState* state = NULL;
  uint8_t (*alloc)[2] = NULL;
  guestFsStatus status = guestFsOk;
  if (volume->device.io.readOnly)
  {
    return guestFsErrReadOnly;
  }
  if (volume->needsReopen)
  {
    return guestFsErrIo;
  }
  if (user != 0)
  {
    return guestFsErrInvalidArgument;
  }
  if (!newName(name, canonical))
  {
    return guestFsErrBadName;
  }
  if (length == 0)
  {
    return guestFsErrEmptyFile;
  }
  chooseType(attributes, &type);
  if (type.headerBytes > 0 && length > maxHeaderedLength)
  {
    return guestFsErrUnsupported;
  }
  uint64_t payload = (uint64_t)length + type.headerBytes;
  if (payload > (uint64_t)sectorTotal * sectorSize)
  {
    return guestFsErrNoSpace;
  }
  uint32_t dataSectors = (uint32_t)((payload + sectorSize - 1) / sectorSize);
  uint32_t listCount = (dataSectors + pairsPerList - 1) / pairsPerList;
  uint32_t totalSectors = dataSectors + listCount;
  state = malloc(sizeof(*state));
  alloc = malloc((size_t)totalSectors * 2);
  if (state == NULL || alloc == NULL)
  {
    status = guestFsErrIo;
    goto finished;
  }
  status = loadState(volume, state);
  if (status != guestFsOk)
  {
    goto finished;
  }
  memset(&found, 0, sizeof(found));
  found.wanted = canonical;
  status = walkCatalog(volume, state->vtoc, findVisit, &found);
  if (status != guestFsOk)
  {
    goto finished;
  }
  if (found.matches > 1)
  {
    status = guestFsErrCorrupt;
    goto finished;
  }
  if (found.matches == 1 && !overwrite)
  {
    status = guestFsErrExists;
    goto finished;
  }
  if (found.matches == 1)
  {
    fileInfo oldInfo;
    status = describeFile(volume, found.match.bytes, &oldInfo);
    if (status == guestFsOk && oldInfo.damaged)
    {
      status = guestFsErrDamagedFile;
    }
    if (status != guestFsOk)
    {
      goto finished;
    }
  }
  if (found.matches == 0 && !found.haveFree)
  {
    status = guestFsErrDirectoryFull;
    goto finished;
  }
  if (collectFreeSectors(state->vtoc, totalSectors, alloc) < totalSectors)
  {
    status = guestFsErrNoSpace;
    goto finished;
  }
  //-- Data goes to free sectors that nothing references yet; the commit below makes it visible.
  status = writeFileSectors(volume, &type, length, alloc, listCount, dataSectors, source, context);
  if (status != guestFsOk)
  {
    goto finished;
  }
  if (!volume->device.io.flush(volume->device.io.context))
  {
    status = guestFsErrIo;
    goto finished;
  }
  if (guestFsCrashPoint("dataWritten"))
  {
    status = guestFsErrAborted;
    volume->needsReopen = true;
    goto finished;
  }
  {
    uint8_t newVtoc[sectorSize];
    uint8_t catalogBuffer[sectorSize];
    const catalogSlot* target = found.matches == 1 ? &found.match : &found.firstFree;
    memcpy(newVtoc, state->vtoc, sectorSize);
    if (found.matches == 1)
    {
      releaseContext release = {newVtoc};
      if (visitFile(volume, target->bytes[0], target->bytes[1], releaseVisit, &release) !=
          guestFsOk)
      {
        status = guestFsErrCorrupt;
        goto finished;
      }
    }
    for (uint32_t index = 0; index < totalSectors; ++index)
    {
      setSectorFree(newVtoc, alloc[index][0], alloc[index][1], false);
    }
    if (!readSector(volume, target->track, target->sector, catalogBuffer))
    {
      status = guestFsErrIo;
      goto finished;
    }
    uint8_t* entry = catalogBuffer + catalogEntryOffset + catalogEntrySize * target->slot;
    entry[0] = alloc[0][0];
    entry[1] = alloc[0][1];
    entry[2] =
        (uint8_t)(type.rawType | ((attributes != NULL && attributes->locked) ? lockedBit : 0));
    encodeName(canonical, entry + nameOffset);
    entry[33] = (uint8_t)(totalSectors & 0xFF);
    entry[34] = (uint8_t)(totalSectors >> 8);
    status = commitVtocAndCatalog(volume, newVtoc, target, catalogBuffer);
  }
finished:
  free(alloc);
  free(state);
  return status;
}

static guestFsStatus dosRemove(guestFsVolume* base, uint8_t user, const char* name)
{
  dosVolume* volume = (dosVolume*)base;
  char wanted[guestFsNameCapacity];
  dosState* state = NULL;
  guestFsStatus status = guestFsOk;
  if (volume->device.io.readOnly)
  {
    return guestFsErrReadOnly;
  }
  if (volume->needsReopen)
  {
    return guestFsErrIo;
  }
  if (user != 0)
  {
    return guestFsErrInvalidArgument;
  }
  if (!lookupName(name, wanted))
  {
    return guestFsErrBadName;
  }
  state = malloc(sizeof(*state));
  if (state == NULL)
  {
    return guestFsErrIo;
  }
  status = loadState(volume, state);
  if (status == guestFsOk)
  {
    findContext found;
    memset(&found, 0, sizeof(found));
    found.wanted = wanted;
    status = walkCatalog(volume, state->vtoc, findVisit, &found);
    if (status == guestFsOk && found.matches == 0)
    {
      status = guestFsErrNotFound;
    }
    else if (status == guestFsOk && found.matches > 1)
    {
      status = guestFsErrCorrupt;
    }
    if (status == guestFsOk)
    {
      fileInfo oldInfo;
      status = describeFile(volume, found.match.bytes, &oldInfo);
      if (status == guestFsOk && oldInfo.damaged)
      {
        status = guestFsErrDamagedFile;
      }
    }
    if (status == guestFsOk)
    {
      uint8_t newVtoc[sectorSize];
      uint8_t catalogBuffer[sectorSize];
      releaseContext release = {newVtoc};
      memcpy(newVtoc, state->vtoc, sectorSize);
      if (visitFile(volume, found.match.bytes[0], found.match.bytes[1], releaseVisit, &release) !=
          guestFsOk)
      {
        status = guestFsErrCorrupt;
      }
      else if (!readSector(volume, found.match.track, found.match.sector, catalogBuffer))
      {
        status = guestFsErrIo;
      }
      else
      {
        uint8_t* entry = catalogBuffer + catalogEntryOffset + catalogEntrySize * found.match.slot;
        entry[nameOffset + nameBytes - 1] = entry[0];
        entry[0] = deletedMarker;
        status = commitVtocAndCatalog(volume, newVtoc, &found.match, catalogBuffer);
      }
    }
  }
  free(state);
  return status;
}

static guestFsStatus dosRename(guestFsVolume* base, uint8_t user, const char* name,
                               const char* newNameText)
{
  dosVolume* volume = (dosVolume*)base;
  char wanted[guestFsNameCapacity];
  char replacement[guestFsNameCapacity];
  dosState* state = NULL;
  guestFsStatus status = guestFsOk;
  if (volume->device.io.readOnly)
  {
    return guestFsErrReadOnly;
  }
  if (volume->needsReopen)
  {
    return guestFsErrIo;
  }
  if (user != 0)
  {
    return guestFsErrInvalidArgument;
  }
  if (!lookupName(name, wanted) || !newName(newNameText, replacement))
  {
    return guestFsErrBadName;
  }
  state = malloc(sizeof(*state));
  if (state == NULL)
  {
    return guestFsErrIo;
  }
  status = loadState(volume, state);
  if (status == guestFsOk)
  {
    findContext found;
    findContext clash;
    memset(&found, 0, sizeof(found));
    memset(&clash, 0, sizeof(clash));
    found.wanted = wanted;
    clash.wanted = replacement;
    status = walkCatalog(volume, state->vtoc, findVisit, &found);
    if (status == guestFsOk)
    {
      status = walkCatalog(volume, state->vtoc, findVisit, &clash);
    }
    if (status == guestFsOk && found.matches == 0)
    {
      status = guestFsErrNotFound;
    }
    else if (status == guestFsOk && found.matches > 1)
    {
      status = guestFsErrCorrupt;
    }
    else if (status == guestFsOk && clash.matches > 0 &&
             !(clash.matches == 1 && clash.match.track == found.match.track &&
               clash.match.sector == found.match.sector && clash.match.slot == found.match.slot))
    {
      status = guestFsErrExists;
    }
    if (status == guestFsOk)
    {
      uint8_t catalogBuffer[sectorSize];
      if (!readSector(volume, found.match.track, found.match.sector, catalogBuffer))
      {
        status = guestFsErrIo;
      }
      else
      {
        uint8_t* entry = catalogBuffer + catalogEntryOffset + catalogEntrySize * found.match.slot;
        guestFsRangeChange change;
        encodeName(replacement, entry + nameOffset);
        guestFsAppleDosSectorChange(&volume->device, found.match.track, found.match.sector,
                                    catalogBuffer, &change);
        status = guestFsRangeCommit(&volume->device.io, volume->journalPath, volume->journalId,
                                    &change, 1, &volume->needsReopen);
      }
    }
  }
  free(state);
  return status;
}

const guestFsOps guestFsDos33Ops = {
    .fsName = "DOS 3.3",
    .supportsOs = dosSupportsOs,
    .open = dosOpen,
    .close = dosClose,
    .info = dosInfo,
    .list = dosList,
    .stat = dosStat,
    .readAt = dosReadAt,
    .write = dosWrite,
    .remove = dosRemove,
    .rename = dosRename,
};
