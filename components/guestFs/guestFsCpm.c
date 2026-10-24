#include "guestFs.h"
#include "guestFsInternal.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

enum
{
  recordSize = 128,
  directoryEntrySize = 32,
  emptyMarker = 0xE5,
  paddingByte = 0x1A,
  nameStorage = 16,
  dataChunkSize = 4096
};

typedef struct
{
  const char* osId;
  const char* profileName;
  uint64_t imageSize;
  uint32_t blockSize;
  uint32_t directoryEntries;
  uint32_t directoryOffset;
  uint32_t maxBlock;
  uint32_t firstDataBlock;
  uint8_t pointerSize;
  uint8_t exm;
} cpmProfile;

//-- Mirrors tools/cpm80/diskImageCpm80.py and tools/cpm86/diskImageCpm86.py PROFILES.
static const cpmProfile cpmProfiles[] = {
    {"cpm80", "SYSTEM", 256256, 1024, 64, 6656, 242, 2, 1, 0},
    {"cpm80", "LARGE", 512512, 2048, 128, 13312, 242, 2, 1, 0},
    {"cpm80", "BIG", 8421376, 16384, 512, 32768, 511, 1, 2, 7},
    {"cpm86", "CPM86", 163840, 1024, 64, 4096, 155, 2, 1, 0},
    {"cpm86", "LARGE", 528384, 2048, 128, 4096, 255, 2, 1, 0},
    {"cpm86", "BIG", 8392704, 16384, 512, 4096, 511, 2, 2, 7},
};

typedef struct
{
  guestFsVolume base;
  guestFsIo io;
  const cpmProfile* profile;
  uint8_t* directory;
  char (*names)[nameStorage];
  uint8_t* blockUsed;
  uint32_t usedBlocks;
  uint32_t freeSlots;
  bool needsReopen;
  char journalPath[320];
  char journalId[guestFsJournalIdCapacity];
} cpmVolume;

typedef struct
{
  uint32_t slot;
  uint8_t oldBytes[directoryEntrySize];
  uint8_t newBytes[directoryEntrySize];
} cpmChange;

typedef struct
{
  uint32_t extents;
  uint32_t maxIndex;
  uint32_t lastRecords;
  uint32_t blocks;
  uint32_t firstSlot;
  bool damaged;
} cpmFile;

static void setDetail(char* detail, size_t capacity, const char* format, uint32_t first,
                      uint32_t second)
{
  if (detail != NULL && capacity > 0)
  {
    snprintf(detail, capacity, format, (unsigned)first, (unsigned)second);
  }
}

static bool nameCharacterValid(char character)
{
  return (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
         character == '_' || character == '$' || character == '-';
}

//-- Validate a CP/M 8.3 name like diskImageCpm.parse_filename; returns the canonical NAME.EXT.
static bool parseName(const char* text, uint8_t packed[11], char canonical[nameStorage])
{
  char upper[16];
  size_t length = strlen(text);
  if (length == 0 || length > 12)
  {
    return false;
  }
  for (size_t index = 0; index <= length; ++index)
  {
    upper[index] = (char)toupper((unsigned char)text[index]);
  }
  const char* dot = strchr(upper, '.');
  size_t baseLength = dot == NULL ? length : (size_t)(dot - upper);
  size_t extensionLength = dot == NULL ? 0 : length - baseLength - 1;
  if (baseLength < 1 || baseLength > 8 || extensionLength > 3)
  {
    return false;
  }
  memset(packed, ' ', 11);
  for (size_t index = 0; index < baseLength; ++index)
  {
    if (!nameCharacterValid(upper[index]))
    {
      return false;
    }
    packed[index] = (uint8_t)upper[index];
  }
  for (size_t index = 0; index < extensionLength; ++index)
  {
    char character = upper[baseLength + 1 + index];
    if (!nameCharacterValid(character))
    {
      return false;
    }
    packed[8 + index] = (uint8_t)character;
  }
  size_t used = 0;
  for (size_t index = 0; index < baseLength; ++index)
  {
    canonical[used++] = upper[index];
  }
  if (extensionLength > 0)
  {
    canonical[used++] = '.';
    for (size_t index = 0; index < extensionLength; ++index)
    {
      canonical[used++] = upper[baseLength + 1 + index];
    }
  }
  canonical[used] = '\0';
  return true;
}

static bool pythonWhitespace(uint8_t character)
{
  return (character >= 9 && character <= 13) || (character >= 28 && character <= 31) ||
         character == ' ';
}

//-- Decode a directory entry name like diskImageCpm._decode_directory_name.
static bool decodeName(const uint8_t* entry, char canonical[nameStorage])
{
  uint8_t packed[11];
  char text[nameStorage];
  uint8_t unused[11];
  size_t baseLength = 8;
  size_t extensionLength = 3;
  for (size_t index = 0; index < 11; ++index)
  {
    packed[index] = entry[1 + index] & 0x7F;
  }
  while (baseLength > 0 && pythonWhitespace(packed[baseLength - 1]))
  {
    --baseLength;
  }
  while (extensionLength > 0 && pythonWhitespace(packed[8 + extensionLength - 1]))
  {
    --extensionLength;
  }
  if (baseLength == 0)
  {
    return false;
  }
  size_t used = 0;
  for (size_t index = 0; index < baseLength; ++index)
  {
    text[used++] = (char)packed[index];
  }
  if (extensionLength > 0)
  {
    text[used++] = '.';
    for (size_t index = 0; index < extensionLength; ++index)
    {
      text[used++] = (char)packed[8 + index];
    }
  }
  text[used] = '\0';
  if (strlen(text) != used)
  {
    return false;
  }
  return parseName(text, unused, canonical);
}

static uint32_t entryExtentNumber(const uint8_t* entry)
{
  return (uint32_t)(entry[12] & 0x1F) | ((uint32_t)(entry[14] & 0x3F) << 5);
}

static uint32_t entryIndex(const cpmProfile* profile, const uint8_t* entry)
{
  return entryExtentNumber(entry) / ((uint32_t)profile->exm + 1);
}

static uint32_t entryRecords(const cpmProfile* profile, const uint8_t* entry)
{
  return (entryExtentNumber(entry) % ((uint32_t)profile->exm + 1)) * recordSize + entry[15];
}

static uint32_t recordsPerEntry(const cpmProfile* profile)
{
  return ((uint32_t)profile->exm + 1) * recordSize;
}

static uint32_t entryBlockCount(const cpmProfile* profile, uint32_t records)
{
  uint32_t perBlock = profile->blockSize / recordSize;
  return (records + perBlock - 1) / perBlock;
}

static uint32_t entryBlock(const cpmProfile* profile, const uint8_t* entry, uint32_t index)
{
  if (profile->pointerSize == 2)
  {
    return (uint32_t)entry[16 + 2 * index] | ((uint32_t)entry[17 + 2 * index] << 8);
  }
  return entry[16 + index];
}

static uint8_t* slotPointer(const cpmVolume* volume, uint32_t slot)
{
  return volume->directory + (size_t)slot * directoryEntrySize;
}

static uint64_t slotOffset(const cpmVolume* volume, uint32_t slot)
{
  return (uint64_t)volume->profile->directoryOffset + (uint64_t)slot * directoryEntrySize;
}

static uint64_t blockOffset(const cpmVolume* volume, uint32_t block)
{
  return (uint64_t)volume->profile->directoryOffset + (uint64_t)block * volume->profile->blockSize;
}

//-- Port of diskImageCpm.inspect_image: fail closed on anything unexpected.
static guestFsStatus scanDirectory(cpmVolume* volume, char* detail, size_t detailCapacity)
{
  const cpmProfile* profile = volume->profile;
  guestFsStatus status = guestFsOk;
  memset(volume->blockUsed, 0, (size_t)profile->maxBlock + 1);
  volume->usedBlocks = 0;
  volume->freeSlots = 0;
  for (uint32_t slot = 0; slot < profile->directoryEntries && status == guestFsOk; ++slot)
  {
    const uint8_t* entry = slotPointer(volume, slot);
    volume->names[slot][0] = '\0';
    if (entry[0] == emptyMarker)
    {
      ++volume->freeSlots;
      continue;
    }
    if (entry[0] > guestFsMaxUser)
    {
      setDetail(detail, detailCapacity, "Invalid CP/M user number %u in directory entry %u",
                entry[0], slot);
      status = guestFsErrCorrupt;
      break;
    }
    if (!decodeName(entry, volume->names[slot]))
    {
      volume->names[slot][0] = '\0';
      setDetail(detail, detailCapacity, "Invalid file name in directory entry %u%.0u", slot, 0);
      status = guestFsErrCorrupt;
      break;
    }
    if (entry[15] > recordSize)
    {
      setDetail(detail, detailCapacity, "Invalid record count in directory entry %u%.0u", slot, 0);
      status = guestFsErrCorrupt;
      break;
    }
    uint32_t blockCount = entryBlockCount(profile, entryRecords(profile, entry));
    if (blockCount > 16u / profile->pointerSize)
    {
      setDetail(detail, detailCapacity, "Too many blocks in directory entry %u%.0u", slot, 0);
      status = guestFsErrCorrupt;
      break;
    }
    for (uint32_t index = 0; index < blockCount; ++index)
    {
      uint32_t block = entryBlock(profile, entry, index);
      if (block < profile->firstDataBlock || block > profile->maxBlock)
      {
        setDetail(detail, detailCapacity, "Invalid allocation block %u in directory entry %u",
                  block, slot);
        status = guestFsErrCorrupt;
        break;
      }
      if (volume->blockUsed[block] != 0)
      {
        setDetail(detail, detailCapacity, "Allocation block %u is used more than once%.0u", block,
                  0);
        status = guestFsErrCorrupt;
        break;
      }
      volume->blockUsed[block] = 1;
      ++volume->usedBlocks;
    }
  }
  for (uint32_t slot = 0; slot < profile->directoryEntries && status == guestFsOk; ++slot)
  {
    if (volume->names[slot][0] == '\0')
    {
      continue;
    }
    for (uint32_t other = slot + 1; other < profile->directoryEntries; ++other)
    {
      if (volume->names[other][0] != '\0' &&
          slotPointer(volume, other)[0] == slotPointer(volume, slot)[0] &&
          strcmp(volume->names[other], volume->names[slot]) == 0 &&
          entryIndex(profile, slotPointer(volume, other)) ==
              entryIndex(profile, slotPointer(volume, slot)))
      {
        setDetail(detail, detailCapacity, "Duplicate extent for user %u in directory entry %u",
                  slotPointer(volume, slot)[0], other);
        status = guestFsErrCorrupt;
        break;
      }
    }
  }
  return status;
}

static guestFsStatus reloadDirectory(cpmVolume* volume, char* detail, size_t detailCapacity)
{
  const cpmProfile* profile = volume->profile;
  if (!volume->io.readAt(volume->io.context, profile->directoryOffset, volume->directory,
                         (size_t)profile->directoryEntries * directoryEntrySize))
  {
    return guestFsErrIo;
  }
  return scanDirectory(volume, detail, detailCapacity);
}

static bool findFile(const cpmVolume* volume, uint8_t user, const char* canonical, cpmFile* file)
{
  const cpmProfile* profile = volume->profile;
  uint32_t entryLimit = recordsPerEntry(profile);
  memset(file, 0, sizeof(*file));
  for (uint32_t slot = 0; slot < profile->directoryEntries; ++slot)
  {
    const uint8_t* entry = slotPointer(volume, slot);
    if (volume->names[slot][0] == '\0' || entry[0] != user ||
        strcmp(volume->names[slot], canonical) != 0)
    {
      continue;
    }
    uint32_t index = entryIndex(profile, entry);
    uint32_t records = entryRecords(profile, entry);
    if (file->extents == 0)
    {
      file->firstSlot = slot;
    }
    if (file->extents == 0 || index > file->maxIndex)
    {
      file->maxIndex = index;
      file->lastRecords = records;
    }
    file->blocks += entryBlockCount(profile, records);
    ++file->extents;
  }
  if (file->extents == 0)
  {
    return false;
  }
  file->damaged = file->extents != file->maxIndex + 1;
  for (uint32_t slot = 0; slot < profile->directoryEntries && !file->damaged; ++slot)
  {
    const uint8_t* entry = slotPointer(volume, slot);
    if (volume->names[slot][0] != '\0' && entry[0] == user &&
        strcmp(volume->names[slot], canonical) == 0 &&
        entryIndex(profile, entry) != file->maxIndex && entryRecords(profile, entry) != entryLimit)
    {
      file->damaged = true;
    }
  }
  return true;
}

static void fillEntry(const cpmVolume* volume, uint8_t user, const char* canonical,
                      const cpmFile* file, guestFsEntry* entry)
{
  const uint8_t* first = slotPointer(volume, file->firstSlot);
  memset(entry, 0, sizeof(*entry));
  strncpy(entry->name, canonical, sizeof(entry->name) - 1);
  entry->user = user;
  entry->size =
      (file->maxIndex * recordsPerEntry(volume->profile) + file->lastRecords) * recordSize;
  entry->allocatedBytes = file->blocks * volume->profile->blockSize;
  entry->readOnlyAttribute = (first[9] & 0x80) != 0;
  entry->systemAttribute = (first[10] & 0x80) != 0;
  entry->damaged = file->damaged;
}

static const cpmProfile* profileFor(const char* osId, uint64_t imageSize, bool* osKnown)
{
  *osKnown = false;
  for (size_t index = 0; index < sizeof(cpmProfiles) / sizeof(cpmProfiles[0]); ++index)
  {
    if (strcmp(cpmProfiles[index].osId, osId) == 0)
    {
      *osKnown = true;
      if (cpmProfiles[index].imageSize == imageSize)
      {
        return &cpmProfiles[index];
      }
    }
  }
  return NULL;
}

static bool cpmSupportsOs(const char* osId)
{
  bool known;
  profileFor(osId, 0, &known);
  return known;
}

static void cpmClose(guestFsVolume* base)
{
  cpmVolume* volume = (cpmVolume*)base;
  free(volume->directory);
  free(volume->names);
  free(volume->blockUsed);
  free(volume);
}

static guestFsStatus cpmOpen(const guestFsIo* io, const char* osId, const char* journalPath,
                             const char* journalId, guestFsVolume** result, char* detail,
                             size_t detailCapacity)
{
  bool known;
  const cpmProfile* profile = profileFor(osId, io->size, &known);
  if (profile == NULL)
  {
    if (detail != NULL && detailCapacity > 0)
    {
      snprintf(detail, detailCapacity, "Image size %llu does not match a known %s geometry",
               (unsigned long long)io->size, osId);
    }
    return guestFsErrUnsupported;
  }
  if (strlen(journalPath) >= sizeof(((cpmVolume*)0)->journalPath) ||
      strlen(journalId) >= guestFsJournalIdCapacity)
  {
    return guestFsErrInvalidArgument;
  }
  cpmVolume* volume = calloc(1, sizeof(*volume));
  if (volume == NULL)
  {
    return guestFsErrIo;
  }
  volume->base.ops = &guestFsCpmOps;
  volume->io = *io;
  volume->profile = profile;
  strcpy(volume->journalPath, journalPath);
  strcpy(volume->journalId, journalId);
  volume->directory = malloc((size_t)profile->directoryEntries * directoryEntrySize);
  volume->names = calloc(profile->directoryEntries, nameStorage);
  volume->blockUsed = calloc((size_t)profile->maxBlock + 1, 1);
  if (volume->directory == NULL || volume->names == NULL || volume->blockUsed == NULL)
  {
    cpmClose(&volume->base);
    return guestFsErrIo;
  }
  guestFsStatus status = reloadDirectory(volume, detail, detailCapacity);
  if (status != guestFsOk)
  {
    cpmClose(&volume->base);
    return status;
  }
  *result = &volume->base;
  return guestFsOk;
}

static void cpmInfo(const guestFsVolume* base, guestFsInfo* info)
{
  const cpmVolume* volume = (const cpmVolume*)base;
  const cpmProfile* profile = volume->profile;
  uint32_t totalBlocks = profile->maxBlock - profile->firstDataBlock + 1;
  memset(info, 0, sizeof(*info));
  info->fsName = "CP/M";
  info->profileName = profile->profileName;
  info->blockSize = profile->blockSize;
  info->totalBlocks = totalBlocks;
  info->freeBlocks = totalBlocks - volume->usedBlocks;
  info->directoryEntries = profile->directoryEntries;
  info->freeDirectoryEntries = volume->freeSlots;
  info->maxUser = guestFsMaxUser;
  info->nameRule = "cpm";
}

static guestFsStatus cpmList(guestFsVolume* base, guestFsListFn callback, void* context)
{
  cpmVolume* volume = (cpmVolume*)base;
  const cpmProfile* profile = volume->profile;
  for (uint32_t slot = 0; slot < profile->directoryEntries; ++slot)
  {
    const uint8_t* entry = slotPointer(volume, slot);
    bool firstOfFile = volume->names[slot][0] != '\0';
    for (uint32_t other = 0; other < slot && firstOfFile; ++other)
    {
      firstOfFile = !(slotPointer(volume, other)[0] == entry[0] &&
                      strcmp(volume->names[other], volume->names[slot]) == 0);
    }
    if (!firstOfFile)
    {
      continue;
    }
    cpmFile file;
    guestFsEntry listed;
    findFile(volume, entry[0], volume->names[slot], &file);
    fillEntry(volume, entry[0], volume->names[slot], &file, &listed);
    if (!callback(context, &listed))
    {
      break;
    }
  }
  return guestFsOk;
}

static guestFsStatus cpmStat(guestFsVolume* base, uint8_t user, const char* name,
                             guestFsEntry* entry)
{
  cpmVolume* volume = (cpmVolume*)base;
  uint8_t packed[11];
  char canonical[nameStorage];
  cpmFile file;
  if (user > guestFsMaxUser)
  {
    return guestFsErrInvalidArgument;
  }
  if (!parseName(name, packed, canonical))
  {
    return guestFsErrBadName;
  }
  if (!findFile(volume, user, canonical, &file))
  {
    return guestFsErrNotFound;
  }
  fillEntry(volume, user, canonical, &file, entry);
  return guestFsOk;
}

static guestFsStatus cpmReadAt(guestFsVolume* base, uint8_t user, const char* name, uint32_t offset,
                               void* buffer, size_t length, size_t* got)
{
  cpmVolume* volume = (cpmVolume*)base;
  const cpmProfile* profile = volume->profile;
  guestFsEntry entry;
  uint8_t* target = buffer;
  uint32_t extentBytes = recordsPerEntry(profile) * recordSize;
  *got = 0;
  guestFsStatus status = cpmStat(base, user, name, &entry);
  if (status != guestFsOk)
  {
    return status;
  }
  if (entry.damaged)
  {
    return guestFsErrDamagedFile;
  }
  uint8_t canonicalPacked[11];
  char canonical[nameStorage];
  parseName(name, canonicalPacked, canonical);
  while (length > 0 && offset < entry.size)
  {
    uint32_t wantedIndex = offset / extentBytes;
    const uint8_t* extent = NULL;
    for (uint32_t slot = 0; slot < profile->directoryEntries && extent == NULL; ++slot)
    {
      const uint8_t* candidate = slotPointer(volume, slot);
      if (volume->names[slot][0] != '\0' && candidate[0] == user &&
          strcmp(volume->names[slot], canonical) == 0 &&
          entryIndex(profile, candidate) == wantedIndex)
      {
        extent = candidate;
      }
    }
    if (extent == NULL)
    {
      return guestFsErrDamagedFile;
    }
    uint32_t within = offset - wantedIndex * extentBytes;
    uint32_t available = entryRecords(profile, extent) * recordSize;
    if (within >= available)
    {
      return guestFsErrDamagedFile;
    }
    while (length > 0 && within < available)
    {
      uint32_t blockIndex = within / profile->blockSize;
      uint32_t blockInside = within % profile->blockSize;
      size_t count = profile->blockSize - blockInside;
      count = count > length ? length : count;
      count = count > available - within ? available - within : count;
      if (!volume->io.readAt(volume->io.context,
                             blockOffset(volume, entryBlock(profile, extent, blockIndex)) +
                                 blockInside,
                             target, count))
      {
        return guestFsErrIo;
      }
      target += count;
      length -= count;
      within += (uint32_t)count;
      offset += (uint32_t)count;
      *got += count;
    }
  }
  return guestFsOk;
}

static bool journalExists(const cpmVolume* volume)
{
  FILE* file = fopen(volume->journalPath, "rb");
  if (file == NULL)
  {
    return false;
  }
  fclose(file);
  return true;
}

static void applyChanges(cpmVolume* volume, const cpmChange* changes, size_t count, bool useNew)
{
  for (size_t index = 0; index < count; ++index)
  {
    memcpy(slotPointer(volume, changes[index].slot),
           useNew ? changes[index].newBytes : changes[index].oldBytes, directoryEntrySize);
  }
}

//-- Validate the changed directory in RAM, then journal, write and commit it.
static guestFsStatus commitChanges(cpmVolume* volume, const cpmChange* changes, size_t count)
{
  guestFsJournal journal;
  guestFsStatus status;
  char detail[guestFsDetailCapacity];
  if (volume->needsReopen || journalExists(volume))
  {
    return guestFsErrIo;
  }
  applyChanges(volume, changes, count, true);
  if (scanDirectory(volume, detail, sizeof(detail)) != guestFsOk)
  {
    applyChanges(volume, changes, count, false);
    scanDirectory(volume, NULL, 0);
    return guestFsErrCorrupt;
  }
  status = guestFsJournalBegin(&journal, volume->journalPath, volume->journalId, volume->io.size);
  for (size_t index = 0; status == guestFsOk && index < count; ++index)
  {
    status = guestFsJournalSave(&journal, &volume->io, slotOffset(volume, changes[index].slot),
                                directoryEntrySize);
  }
  if (status == guestFsOk)
  {
    status = guestFsJournalSeal(&journal);
  }
  if (status != guestFsOk)
  {
    if (status == guestFsErrAborted)
    {
      guestFsJournalAbandon(&journal);
      volume->needsReopen = true;
    }
    else
    {
      guestFsJournalAbandon(&journal);
      remove(volume->journalPath);
      applyChanges(volume, changes, count, false);
      scanDirectory(volume, NULL, 0);
    }
    return status;
  }
  if (guestFsCrashPoint("sealed"))
  {
    guestFsJournalAbandon(&journal);
    volume->needsReopen = true;
    return guestFsErrAborted;
  }
  for (size_t index = 0; status == guestFsOk && index < count; ++index)
  {
    if (!volume->io.writeAt(volume->io.context, slotOffset(volume, changes[index].slot),
                            changes[index].newBytes, directoryEntrySize))
    {
      status = guestFsErrIo;
    }
    else if (guestFsCrashPoint("slotWritten"))
    {
      guestFsJournalAbandon(&journal);
      volume->needsReopen = true;
      return guestFsErrAborted;
    }
  }
  if (status == guestFsOk && !volume->io.flush(volume->io.context))
  {
    status = guestFsErrIo;
  }
  if (status == guestFsOk && guestFsCrashPoint("applied"))
  {
    guestFsJournalAbandon(&journal);
    volume->needsReopen = true;
    return guestFsErrAborted;
  }
  if (status == guestFsOk)
  {
    status = guestFsJournalCommit(&journal);
  }
  if (status == guestFsOk && guestFsCrashPoint("committed"))
  {
    guestFsJournalAbandon(&journal);
    volume->needsReopen = true;
    return guestFsErrAborted;
  }
  if (status == guestFsOk)
  {
    if (guestFsJournalFinish(&journal) != guestFsOk)
    {
      volume->needsReopen = true;
    }
    return guestFsOk;
  }
  guestFsJournalAbandon(&journal);
  if (guestFsJournalRecover(volume->journalPath, &volume->io) != guestFsOk ||
      reloadDirectory(volume, NULL, 0) != guestFsOk)
  {
    volume->needsReopen = true;
  }
  return status;
}

static size_t findSlots(const cpmVolume* volume, uint8_t user, const char* canonical,
                        uint32_t* slots, size_t capacity)
{
  size_t count = 0;
  for (uint32_t slot = 0; slot < volume->profile->directoryEntries && count < capacity; ++slot)
  {
    if (volume->names[slot][0] != '\0' && slotPointer(volume, slot)[0] == user &&
        strcmp(volume->names[slot], canonical) == 0)
    {
      slots[count++] = slot;
    }
  }
  return count;
}

static void buildEntry(const cpmProfile* profile, uint8_t* entry, uint8_t user,
                       const uint8_t packed[11], uint32_t extent, uint32_t totalRecords,
                       const uint16_t* allocated)
{
  uint32_t perBlock = profile->blockSize / recordSize;
  uint32_t entryLimit = recordsPerEntry(profile);
  uint32_t firstRecord = extent * entryLimit;
  uint32_t extentRecords =
      totalRecords - firstRecord < entryLimit ? totalRecords - firstRecord : entryLimit;
  uint32_t firstBlock = firstRecord / perBlock;
  uint32_t blockCount = (extentRecords + perBlock - 1) / perBlock;
  uint32_t logicalExtents = (extentRecords + recordSize - 1) / recordSize;
  uint32_t extentNumber = extent * ((uint32_t)profile->exm + 1) + logicalExtents - 1;
  memset(entry, 0, directoryEntrySize);
  entry[0] = user;
  memcpy(entry + 1, packed, 11);
  entry[12] = (uint8_t)(extentNumber & 0x1F);
  entry[14] = (uint8_t)((extentNumber >> 5) & 0x3F);
  entry[15] = (uint8_t)(extentRecords - (logicalExtents - 1) * recordSize);
  for (uint32_t index = 0; index < blockCount; ++index)
  {
    uint16_t block = allocated[firstBlock + index];
    if (profile->pointerSize == 2)
    {
      entry[16 + 2 * index] = (uint8_t)(block & 0xFF);
      entry[17 + 2 * index] = (uint8_t)(block >> 8);
    }
    else
    {
      entry[16 + index] = (uint8_t)block;
    }
  }
}

static guestFsStatus cpmWrite(guestFsVolume* base, uint8_t user, const char* name, uint32_t length,
                              guestFsSourceFn source, void* context, bool overwrite,
                              const guestFsAttributes* attributes)
{
  cpmVolume* volume = (cpmVolume*)base;
  (void)attributes;
  const cpmProfile* profile = volume->profile;
  uint8_t packed[11];
  char canonical[nameStorage];
  uint32_t* oldSlots = NULL;
  uint16_t* allocated = NULL;
  uint8_t* chunk = NULL;
  cpmChange* changes = NULL;
  guestFsStatus status = guestFsOk;
  if (volume->io.readOnly)
  {
    return guestFsErrReadOnly;
  }
  if (volume->needsReopen)
  {
    return guestFsErrIo;
  }
  if (user > guestFsMaxUser)
  {
    return guestFsErrInvalidArgument;
  }
  if (!parseName(name, packed, canonical))
  {
    return guestFsErrBadName;
  }
  if (length == 0)
  {
    return guestFsErrEmptyFile;
  }
  uint32_t perBlock = profile->blockSize / recordSize;
  uint32_t totalRecords = (uint32_t)(((uint64_t)length + recordSize - 1) / recordSize);
  uint32_t blocksNeeded = (totalRecords + perBlock - 1) / perBlock;
  uint32_t entryLimit = recordsPerEntry(profile);
  uint32_t extentCount = (totalRecords + entryLimit - 1) / entryLimit;
  uint32_t freeBlocks = profile->maxBlock - profile->firstDataBlock + 1 - volume->usedBlocks;
  oldSlots = malloc(sizeof(uint32_t) * profile->directoryEntries);
  allocated = malloc(sizeof(uint16_t) * blocksNeeded);
  if (oldSlots == NULL || allocated == NULL)
  {
    status = guestFsErrIo;
    goto finished;
  }
  size_t oldCount = findSlots(volume, user, canonical, oldSlots, profile->directoryEntries);
  if (oldCount > 0 && !overwrite)
  {
    status = guestFsErrExists;
    goto finished;
  }
  if (blocksNeeded > freeBlocks)
  {
    status = guestFsErrNoSpace;
    goto finished;
  }
  if (extentCount > volume->freeSlots + oldCount)
  {
    status = guestFsErrDirectoryFull;
    goto finished;
  }
  uint32_t found = 0;
  for (uint32_t block = profile->firstDataBlock; block <= profile->maxBlock && found < blocksNeeded;
       ++block)
  {
    if (volume->blockUsed[block] == 0)
    {
      allocated[found++] = (uint16_t)block;
    }
  }
  uint32_t chunkSize = profile->blockSize < dataChunkSize ? profile->blockSize : dataChunkSize;
  chunk = malloc(chunkSize);
  if (chunk == NULL)
  {
    status = guestFsErrIo;
    goto finished;
  }
  //-- Data goes to unreferenced free blocks first; the directory commit below makes it visible.
  for (uint32_t index = 0; index < blocksNeeded; ++index)
  {
    uint64_t blockStart = (uint64_t)index * profile->blockSize;
    for (uint32_t inside = 0; inside < profile->blockSize; inside += chunkSize)
    {
      uint32_t piece =
          profile->blockSize - inside < chunkSize ? profile->blockSize - inside : chunkSize;
      uint64_t position = blockStart + inside;
      uint32_t have = position >= length
                          ? 0
                          : (length - position < piece ? (uint32_t)(length - position) : piece);
      if (have > 0 && !source(context, chunk, have))
      {
        status = guestFsErrAborted;
        goto finished;
      }
      memset(chunk + have, paddingByte, piece - have);
      if (!volume->io.writeAt(volume->io.context, blockOffset(volume, allocated[index]) + inside,
                              chunk, piece))
      {
        status = guestFsErrIo;
        goto finished;
      }
    }
  }
  if (!volume->io.flush(volume->io.context))
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
  changes = calloc(extentCount + oldCount, sizeof(cpmChange));
  if (changes == NULL)
  {
    status = guestFsErrIo;
    goto finished;
  }
  size_t changeCount = 0;
  uint32_t extent = 0;
  for (uint32_t slot = 0; slot < profile->directoryEntries && extent < extentCount; ++slot)
  {
    bool slotFree = slotPointer(volume, slot)[0] == emptyMarker;
    bool slotOld = false;
    for (size_t index = 0; index < oldCount && !slotOld; ++index)
    {
      slotOld = oldSlots[index] == slot;
    }
    if (!slotFree && !slotOld)
    {
      continue;
    }
    changes[changeCount].slot = slot;
    memcpy(changes[changeCount].oldBytes, slotPointer(volume, slot), directoryEntrySize);
    buildEntry(profile, changes[changeCount].newBytes, user, packed, extent, totalRecords,
               allocated);
    ++changeCount;
    ++extent;
  }
  for (size_t index = 0; index < oldCount; ++index)
  {
    bool reused = false;
    for (size_t change = 0; change < changeCount && !reused; ++change)
    {
      reused = changes[change].slot == oldSlots[index];
    }
    if (!reused)
    {
      changes[changeCount].slot = oldSlots[index];
      memcpy(changes[changeCount].oldBytes, slotPointer(volume, oldSlots[index]),
             directoryEntrySize);
      memcpy(changes[changeCount].newBytes, changes[changeCount].oldBytes, directoryEntrySize);
      changes[changeCount].newBytes[0] = emptyMarker;
      ++changeCount;
    }
  }
  status = commitChanges(volume, changes, changeCount);
finished:
  free(changes);
  free(chunk);
  free(allocated);
  free(oldSlots);
  return status;
}

static guestFsStatus changeSlots(cpmVolume* volume, uint8_t user, const char* name,
                                 const char* newName, bool deleteFile)
{
  uint8_t packed[11];
  uint8_t newPacked[11];
  char canonical[nameStorage];
  char newCanonical[nameStorage];
  if (volume->io.readOnly)
  {
    return guestFsErrReadOnly;
  }
  if (volume->needsReopen)
  {
    return guestFsErrIo;
  }
  if (user > guestFsMaxUser)
  {
    return guestFsErrInvalidArgument;
  }
  if (!parseName(name, packed, canonical) ||
      (!deleteFile && !parseName(newName, newPacked, newCanonical)))
  {
    return guestFsErrBadName;
  }
  uint32_t* slots = malloc(sizeof(uint32_t) * volume->profile->directoryEntries);
  cpmChange* changes = NULL;
  uint32_t clash[1];
  guestFsStatus status = guestFsOk;
  if (slots == NULL)
  {
    return guestFsErrIo;
  }
  size_t count = findSlots(volume, user, canonical, slots, volume->profile->directoryEntries);
  if (count == 0)
  {
    status = guestFsErrNotFound;
  }
  else if (!deleteFile && strcmp(canonical, newCanonical) != 0 &&
           findSlots(volume, user, newCanonical, clash, 1) > 0)
  {
    status = guestFsErrExists;
  }
  else if (!deleteFile && strcmp(canonical, newCanonical) == 0)
  {
    status = guestFsOk;
    count = 0;
  }
  if (status == guestFsOk && count > 0)
  {
    changes = calloc(count, sizeof(cpmChange));
    if (changes == NULL)
    {
      status = guestFsErrIo;
    }
    for (size_t index = 0; status == guestFsOk && index < count; ++index)
    {
      changes[index].slot = slots[index];
      memcpy(changes[index].oldBytes, slotPointer(volume, slots[index]), directoryEntrySize);
      memcpy(changes[index].newBytes, changes[index].oldBytes, directoryEntrySize);
      if (deleteFile)
      {
        changes[index].newBytes[0] = emptyMarker;
      }
      else
      {
        for (size_t character = 0; character < 11; ++character)
        {
          changes[index].newBytes[1 + character] =
              (uint8_t)(newPacked[character] | (changes[index].oldBytes[1 + character] & 0x80));
        }
      }
    }
    if (status == guestFsOk)
    {
      status = commitChanges(volume, changes, count);
    }
  }
  free(changes);
  free(slots);
  return status;
}

static guestFsStatus cpmRemove(guestFsVolume* base, uint8_t user, const char* name)
{
  return changeSlots((cpmVolume*)base, user, name, NULL, true);
}

static guestFsStatus cpmRename(guestFsVolume* base, uint8_t user, const char* name,
                               const char* newName)
{
  return changeSlots((cpmVolume*)base, user, name, newName, false);
}

const guestFsOps guestFsCpmOps = {
    .fsName = "CP/M",
    .supportsOs = cpmSupportsOs,
    .open = cpmOpen,
    .close = cpmClose,
    .info = cpmInfo,
    .list = cpmList,
    .stat = cpmStat,
    .readAt = cpmReadAt,
    .write = cpmWrite,
    .remove = cpmRemove,
    .rename = cpmRename,
};
