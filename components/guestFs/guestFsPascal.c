#include "guestFs.h"
#include "guestFsInternal.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-- UCSD Pascal volumes on 140K / 640K / 800K Apple II images (ProDOS 512-byte blocks).
//-- Reference: tools/apple2/extractApple2Files.py PascalVolume. The directory is re-read from the
//image for every
//-- operation, so the volume object holds no cached view that could go stale.
enum
{
  slotBytes = 26,
  firstDirectoryBlock = 2,
  minDirectoryEnd = 3,
  maxDirectoryEnd = 32,
  maxNameLength = 15,
  maxVolumeNameLength = 7,
  minTotalBlocks = 6,
  chunkBlocks = 8,
  kindBad = 1,
  kindSecondaryDirectory = 8,
  noEntry = 0xFFFF
};

typedef struct
{
  guestFsVolume base;
  guestFsAppleDevice device;
  bool needsReopen;
  char journalPath[320];
  char journalId[guestFsJournalIdCapacity];
} pascalVolume;

//-- Image of the directory blocks 2..dirEnd-1; entry slot i (0 based) is at raw + slotBytes * (i +
// 1).
typedef struct
{
  uint8_t* raw;
  uint16_t* order;
  uint32_t dirEnd;
  uint32_t dirBlocks;
  uint32_t totalBlocks;
  uint32_t capacity;
  uint32_t count;
  uint32_t usedBlocks;
} pascalDirectory;

static const char* const kindLabels[] = {NULL,   "BAD",  "CODE", "TEXT",  "INFO",
                                         "DATA", "GRAF", "FOTO", "SECDIR"};

static uint16_t le16(const uint8_t* bytes)
{
  return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static void putLe16(uint8_t* bytes, uint32_t value)
{
  bytes[0] = (uint8_t)(value & 0xFF);
  bytes[1] = (uint8_t)((value >> 8) & 0xFF);
}

static void setDetail(char* detail, size_t capacity, const char* format, ...)
    __attribute__((format(printf, 3, 4)));

static void setDetail(char* detail, size_t capacity, const char* format, ...)
{
  if (detail == NULL || capacity == 0)
  {
    return;
  }
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(detail, capacity, format, arguments);
  va_end(arguments);
}

static uint8_t* slotAt(const pascalDirectory* directory, uint32_t index)
{
  return directory->raw + (size_t)slotBytes * (index + 1);
}

static uint32_t slotSize(const uint8_t* slot)
{
  return (uint32_t)(le16(slot + 2) - le16(slot) - 1) * guestFsAppleBlockSize + le16(slot + 22);
}

static uint8_t slotKind(const uint8_t* slot)
{
  return (uint8_t)(le16(slot + 4) & 0x0F);
}

static bool kindValid(uint8_t kind)
{
  return kind >= kindBad && kind <= kindSecondaryDirectory;
}

static void freeDirectory(pascalDirectory* directory)
{
  free(directory->raw);
  free(directory->order);
  memset(directory, 0, sizeof(*directory));
}

static bool bytesPrintable(const uint8_t* bytes, size_t length, uint8_t lowest)
{
  for (size_t index = 0; index < length; ++index)
  {
    if (bytes[index] < lowest || bytes[index] > 0x7E)
    {
      return false;
    }
  }
  return true;
}

static bool namesEqual(const uint8_t* slot, const char* canonical, size_t canonicalLength)
{
  if (slot[6] != canonicalLength)
  {
    return false;
  }
  for (size_t index = 0; index < canonicalLength; ++index)
  {
    if (toupper(slot[7 + index]) != canonical[index])
    {
      return false;
    }
  }
  return true;
}

//-- Reads and fully validates the directory. guestFsErrUnsupported (detail untouched) means "not a
// Pascal
//-- volume"; guestFsErrCorrupt means it is one but the directory cannot be trusted.
static guestFsStatus loadDirectory(const pascalVolume* volume, pascalDirectory* directory,
                                   char* detail, size_t detailCapacity)
{
  uint8_t header[guestFsAppleBlockSize];
  const guestFsAppleDevice* device = &volume->device;
  uint32_t deviceBlocks = guestFsAppleBlockCount(device);
  memset(directory, 0, sizeof(*directory));
  if (!guestFsAppleReadBlock(device, firstDirectoryBlock, header))
  {
    return guestFsErrIo;
  }
  uint32_t nameLength = header[6];
  uint32_t totalBlocks = le16(header + 14);
  if (le16(header) != 0 || le16(header + 4) != 0 || nameLength < 1 ||
      nameLength > maxVolumeNameLength || totalBlocks < minTotalBlocks ||
      !bytesPrintable(header + 7, nameLength, 0x21))
  {
    return guestFsErrUnsupported;
  }
  uint32_t dirEnd = le16(header + 2);
  if (dirEnd < minDirectoryEnd || dirEnd > maxDirectoryEnd)
  {
    setDetail(detail, detailCapacity, "Pascal directory end block %u is outside %u..%u",
              (unsigned)dirEnd, (unsigned)minDirectoryEnd, (unsigned)maxDirectoryEnd);
    return guestFsErrCorrupt;
  }
  if (totalBlocks > deviceBlocks || totalBlocks < dirEnd)
  {
    setDetail(detail, detailCapacity, "Pascal volume claims %u blocks but the image holds %u",
              (unsigned)totalBlocks, (unsigned)deviceBlocks);
    return guestFsErrCorrupt;
  }
  uint32_t dirBlocks = dirEnd - firstDirectoryBlock;
  uint32_t capacity = dirBlocks * guestFsAppleBlockSize / slotBytes - 1;
  uint32_t count = le16(header + 16);
  if (count > capacity)
  {
    setDetail(detail, detailCapacity, "Pascal file count %u exceeds the directory capacity %u",
              (unsigned)count, (unsigned)capacity);
    return guestFsErrCorrupt;
  }
  directory->raw = malloc((size_t)dirBlocks * guestFsAppleBlockSize);
  directory->order = malloc(sizeof(uint16_t) * (count == 0 ? 1 : count));
  if (directory->raw == NULL || directory->order == NULL)
  {
    freeDirectory(directory);
    return guestFsErrIo;
  }
  for (uint32_t block = 0; block < dirBlocks; ++block)
  {
    if (!guestFsAppleReadBlock(device, firstDirectoryBlock + block,
                               directory->raw + (size_t)block * guestFsAppleBlockSize))
    {
      freeDirectory(directory);
      return guestFsErrIo;
    }
  }
  directory->dirEnd = dirEnd;
  directory->dirBlocks = dirBlocks;
  directory->totalBlocks = totalBlocks;
  directory->capacity = capacity;
  directory->count = count;
  guestFsStatus status = guestFsOk;
  for (uint32_t index = 0; index < count && status == guestFsOk; ++index)
  {
    const uint8_t* slot = slotAt(directory, index);
    uint32_t first = le16(slot);
    uint32_t next = le16(slot + 2);
    uint32_t lastBytes = le16(slot + 22);
    if (first < dirEnd || next <= first || next > totalBlocks)
    {
      setDetail(detail, detailCapacity, "Pascal entry %u has blocks %u..%u outside the data area",
                (unsigned)index, (unsigned)first, (unsigned)next);
      status = guestFsErrCorrupt;
    }
    else if (slot[6] < 1 || slot[6] > maxNameLength || !bytesPrintable(slot + 7, slot[6], 0x20))
    {
      setDetail(detail, detailCapacity, "Pascal entry %u has an invalid file name",
                (unsigned)index);
      status = guestFsErrCorrupt;
    }
    else if (lastBytes < 1 || lastBytes > guestFsAppleBlockSize)
    {
      setDetail(detail, detailCapacity, "Pascal entry %u has %u bytes in its last block",
                (unsigned)index, (unsigned)lastBytes);
      status = guestFsErrCorrupt;
    }
    directory->usedBlocks += next - first;
  }
  //-- Sort the entries by first block (insertion sort, the directory holds at most a few hundred).
  for (uint32_t index = 0; index < count && status == guestFsOk; ++index)
  {
    uint32_t position = index;
    while (position > 0 &&
           le16(slotAt(directory, directory->order[position - 1])) > le16(slotAt(directory, index)))
    {
      directory->order[position] = directory->order[position - 1];
      --position;
    }
    directory->order[position] = (uint16_t)index;
  }
  for (uint32_t position = 1; position < count && status == guestFsOk; ++position)
  {
    const uint8_t* before = slotAt(directory, directory->order[position - 1]);
    const uint8_t* after = slotAt(directory, directory->order[position]);
    if (le16(before + 2) > le16(after))
    {
      setDetail(detail, detailCapacity, "Pascal entries %u and %u overlap",
                (unsigned)directory->order[position - 1], (unsigned)directory->order[position]);
      status = guestFsErrCorrupt;
    }
  }
  for (uint32_t index = 0; index < count && status == guestFsOk; ++index)
  {
    const uint8_t* slot = slotAt(directory, index);
    char upper[maxNameLength];
    for (uint32_t character = 0; character < slot[6]; ++character)
    {
      upper[character] = (char)toupper(slot[7 + character]);
    }
    for (uint32_t other = index + 1; other < count && status == guestFsOk; ++other)
    {
      if (namesEqual(slotAt(directory, other), upper, slot[6]))
      {
        setDetail(detail, detailCapacity, "Pascal entries %u and %u have the same name",
                  (unsigned)index, (unsigned)other);
        status = guestFsErrCorrupt;
      }
    }
  }
  if (status != guestFsOk)
  {
    freeDirectory(directory);
  }
  return status;
}

//-- Lookup is lenient (any listed name, case-insensitive); creating names are checked by
// parseNewName.
static uint32_t findEntry(const pascalDirectory* directory, const char* name)
{
  size_t length = strlen(name);
  char upper[maxNameLength];
  if (length < 1 || length > maxNameLength)
  {
    return noEntry;
  }
  for (size_t index = 0; index < length; ++index)
  {
    upper[index] = (char)toupper((unsigned char)name[index]);
  }
  for (uint32_t index = 0; index < directory->count; ++index)
  {
    if (namesEqual(slotAt(directory, index), upper, length))
    {
      return index;
    }
  }
  return noEntry;
}

//-- Same rule as extractApple2Files.sanitizePascalName, but invalid names are refused instead of
// cleaned.
static bool parseNewName(const char* text, char canonical[maxNameLength + 1])
{
  size_t length = strlen(text);
  if (length < 1 || length > maxNameLength)
  {
    return false;
  }
  for (size_t index = 0; index < length; ++index)
  {
    int character = toupper((unsigned char)text[index]);
    if (character < 0x21 || character > 0x7E || strchr("$=?,[#:/", character) != NULL)
    {
      return false;
    }
    canonical[index] = (char)character;
  }
  canonical[length] = '\0';
  return true;
}

static uint8_t kindForNewFile(const char* canonical, const guestFsAttributes* attributes)
{
  static const struct
  {
    const char* suffix;
    uint8_t kind;
  } suffixes[] = {{".TEXT", 3}, {".CODE", 2}, {".DATA", 5},
                  {".INFO", 4}, {".GRAF", 6}, {".FOTO", 7}};
  if (attributes != NULL && attributes->pascalKind >= 2 &&
      attributes->pascalKind <= kindSecondaryDirectory)
  {
    return attributes->pascalKind;
  }
  const char* dot = strrchr(canonical, '.');
  for (size_t index = 0; dot != NULL && index < sizeof(suffixes) / sizeof(suffixes[0]); ++index)
  {
    if (strcmp(dot, suffixes[index].suffix) == 0)
    {
      return suffixes[index].kind;
    }
  }
  return 5;
}

static void fillEntry(const uint8_t* slot, guestFsEntry* entry)
{
  uint8_t kind = slotKind(slot);
  memset(entry, 0, sizeof(*entry));
  memcpy(entry->name, slot + 7, slot[6]);
  entry->size = slotSize(slot);
  entry->allocatedBytes = (uint32_t)(le16(slot + 2) - le16(slot)) * guestFsAppleBlockSize;
  entry->damaged = !kindValid(kind);
  if (kindValid(kind))
  {
    snprintf(entry->typeLabel, sizeof(entry->typeLabel), "%s", kindLabels[kind]);
  }
  else
  {
    snprintf(entry->typeLabel, sizeof(entry->typeLabel), "#%u", (unsigned)kind);
  }
  entry->hasAttributes = true;
  entry->attributes.pascalKind = kind;
  entry->attributes.prodosType = kind == 2 ? 0x0B : (kind == 3 ? 0x0C : (kind == 5 ? 0x0D : 0x06));
}

static bool opAllowed(const pascalVolume* volume, uint8_t user, bool writing, guestFsStatus* status)
{
  if (writing && volume->device.io.readOnly)
  {
    *status = guestFsErrReadOnly;
    return false;
  }
  if (volume->needsReopen)
  {
    *status = guestFsErrIo;
    return false;
  }
  if (user != 0)
  {
    *status = guestFsErrInvalidArgument;
    return false;
  }
  return true;
}

static bool pascalSupportsOs(const char* osId)
{
  return guestFsAppleSupportsOs(osId);
}

static void pascalClose(guestFsVolume* base)
{
  free(base);
}

static guestFsStatus pascalOpen(const guestFsIo* io, const char* osId, const char* journalPath,
                                const char* journalId, guestFsVolume** result, char* detail,
                                size_t detailCapacity)
{
  (void)osId;
  pascalVolume* volume = NULL;
  if (strlen(journalPath) >= sizeof(volume->journalPath) ||
      strlen(journalId) >= guestFsJournalIdCapacity)
  {
    return guestFsErrInvalidArgument;
  }
  volume = calloc(1, sizeof(*volume));
  if (volume == NULL)
  {
    return guestFsErrIo;
  }
  guestFsStatus status =
      guestFsAppleOpenDevice(&volume->device, io, journalId, detail, detailCapacity);
  pascalDirectory directory;
  if (status == guestFsOk)
  {
    status = loadDirectory(volume, &directory, detail, detailCapacity);
  }
  if (status != guestFsOk)
  {
    free(volume);
    return status;
  }
  freeDirectory(&directory);
  volume->base.ops = &guestFsPascalOps;
  strcpy(volume->journalPath, journalPath);
  strcpy(volume->journalId, journalId);
  *result = &volume->base;
  return guestFsOk;
}

static void pascalInfo(const guestFsVolume* base, guestFsInfo* info)
{
  const pascalVolume* volume = (const pascalVolume*)base;
  pascalDirectory directory;
  memset(info, 0, sizeof(*info));
  info->fsName = "UCSD Pascal";
  info->blockSize = guestFsAppleBlockSize;
  info->nameRule = "pascal";
  info->maxUser = 0;
  switch (volume->device.io.size)
  {
  case guestFsAppleDosImageSize:
    info->profileName = "140K";
    break;
  case 655360:
    info->profileName = "640K";
    break;
  default:
    info->profileName = "800K";
    break;
  }
  if (loadDirectory(volume, &directory, NULL, 0) != guestFsOk)
  {
    return;
  }
  info->totalBlocks = directory.totalBlocks - directory.dirEnd;
  info->freeBlocks = info->totalBlocks - directory.usedBlocks;
  info->directoryEntries = directory.capacity;
  info->freeDirectoryEntries = directory.capacity - directory.count;
  freeDirectory(&directory);
}

static guestFsStatus pascalList(guestFsVolume* base, guestFsListFn callback, void* context)
{
  pascalVolume* volume = (pascalVolume*)base;
  pascalDirectory directory;
  guestFsStatus status = guestFsOk;
  if (!opAllowed(volume, 0, false, &status))
  {
    return status;
  }
  status = loadDirectory(volume, &directory, NULL, 0);
  for (uint32_t index = 0; status == guestFsOk && index < directory.count; ++index)
  {
    guestFsEntry entry;
    fillEntry(slotAt(&directory, index), &entry);
    if (!callback(context, &entry))
    {
      break;
    }
  }
  if (status == guestFsOk)
  {
    freeDirectory(&directory);
  }
  return status;
}

static guestFsStatus pascalStat(guestFsVolume* base, uint8_t user, const char* name,
                                guestFsEntry* entry)
{
  pascalVolume* volume = (pascalVolume*)base;
  pascalDirectory directory;
  guestFsStatus status = guestFsOk;
  if (!opAllowed(volume, user, false, &status))
  {
    return status;
  }
  status = loadDirectory(volume, &directory, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t index = findEntry(&directory, name);
  if (index == noEntry)
  {
    status = guestFsErrNotFound;
  }
  else
  {
    fillEntry(slotAt(&directory, index), entry);
  }
  freeDirectory(&directory);
  return status;
}

static guestFsStatus pascalReadAt(guestFsVolume* base, uint8_t user, const char* name,
                                  uint32_t offset, void* buffer, size_t length, size_t* got)
{
  pascalVolume* volume = (pascalVolume*)base;
  pascalDirectory directory;
  uint8_t block[guestFsAppleBlockSize];
  guestFsStatus status = guestFsOk;
  *got = 0;
  if (!opAllowed(volume, user, false, &status))
  {
    return status;
  }
  status = loadDirectory(volume, &directory, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t index = findEntry(&directory, name);
  if (index == noEntry)
  {
    status = guestFsErrNotFound;
  }
  else if (!kindValid(slotKind(slotAt(&directory, index))))
  {
    status = guestFsErrDamagedFile;
  }
  else
  {
    const uint8_t* slot = slotAt(&directory, index);
    uint32_t size = slotSize(slot);
    uint32_t first = le16(slot);
    uint8_t* target = buffer;
    uint32_t position = offset;
    if (offset < size)
    {
      size_t remaining = size - offset < length ? size - offset : length;
      while (remaining > 0 && status == guestFsOk)
      {
        uint32_t inside = position % guestFsAppleBlockSize;
        size_t piece =
            guestFsAppleBlockSize - inside < remaining ? guestFsAppleBlockSize - inside : remaining;
        if (!guestFsAppleReadBlock(&volume->device, first + position / guestFsAppleBlockSize,
                                   block))
        {
          status = guestFsErrIo;
          break;
        }
        memcpy(target + *got, block + inside, piece);
        *got += piece;
        position += (uint32_t)piece;
        remaining -= piece;
      }
    }
  }
  freeDirectory(&directory);
  return status;
}

//-- Rebuild the directory blocks: the entries sorted by first block (as
// extractApple2Files.writeDirectory),
//-- optionally without skipIndex and with newSlot inserted. Header and entry bytes are copied
// verbatim.
static void buildDirectory(const pascalDirectory* directory, uint8_t* newRaw, uint32_t skipIndex,
                           const uint8_t* newSlot)
{
  uint32_t out = 0;
  bool inserted = newSlot == NULL;
  memcpy(newRaw, directory->raw, (size_t)directory->dirBlocks * guestFsAppleBlockSize);
  memset(newRaw + slotBytes, 0, (size_t)slotBytes * directory->capacity);
  for (uint32_t position = 0; position < directory->count; ++position)
  {
    uint32_t index = directory->order[position];
    if (index == skipIndex)
    {
      continue;
    }
    const uint8_t* slot = slotAt(directory, index);
    if (!inserted && le16(newSlot) < le16(slot))
    {
      memcpy(newRaw + (size_t)slotBytes * (out + 1), newSlot, slotBytes);
      ++out;
      inserted = true;
    }
    memcpy(newRaw + (size_t)slotBytes * (out + 1), slot, slotBytes);
    ++out;
  }
  if (!inserted)
  {
    memcpy(newRaw + (size_t)slotBytes * (out + 1), newSlot, slotBytes);
    ++out;
  }
  putLe16(newRaw + 16, out);
}

//-- Journaled commit of every directory block that differs from the one just read.
static guestFsStatus commitDirectory(pascalVolume* volume, const pascalDirectory* directory,
                                     const uint8_t* newRaw)
{
  guestFsRangeChange* changes = calloc(2U * directory->dirBlocks, sizeof(*changes));
  size_t count = 0;
  if (changes == NULL)
  {
    return guestFsErrIo;
  }
  for (uint32_t block = 0; block < directory->dirBlocks; ++block)
  {
    size_t offset = (size_t)block * guestFsAppleBlockSize;
    if (memcmp(directory->raw + offset, newRaw + offset, guestFsAppleBlockSize) != 0)
    {
      count += guestFsAppleBlockChanges(&volume->device, firstDirectoryBlock + block,
                                        newRaw + offset, &changes[count]);
    }
  }
  guestFsStatus status = guestFsOk;
  if (count > 0)
  {
    status = guestFsRangeCommit(&volume->device.io, volume->journalPath, volume->journalId, changes,
                                count, &volume->needsReopen);
  }
  free(changes);
  return status;
}

//-- First fit exactly like PascalVolume.importFile: gaps in first-block order, then the tail.
static bool findGap(const pascalDirectory* directory, uint32_t needed, uint32_t* position)
{
  uint32_t cursor = directory->dirEnd;
  for (uint32_t index = 0; index < directory->count; ++index)
  {
    const uint8_t* slot = slotAt(directory, directory->order[index]);
    if (le16(slot) - cursor >= needed)
    {
      *position = cursor;
      return true;
    }
    if (le16(slot + 2) > cursor)
    {
      cursor = le16(slot + 2);
    }
  }
  if (directory->totalBlocks - cursor >= needed)
  {
    *position = cursor;
    return true;
  }
  return false;
}

static guestFsStatus pascalWrite(guestFsVolume* base, uint8_t user, const char* name,
                                 uint32_t length, guestFsSourceFn source, void* context,
                                 bool overwrite, const guestFsAttributes* attributes)
{
  pascalVolume* volume = (pascalVolume*)base;
  pascalDirectory directory;
  char canonical[maxNameLength + 1];
  uint8_t* chunk = NULL;
  uint8_t* newRaw = NULL;
  guestFsStatus status = guestFsOk;
  if (!opAllowed(volume, user, true, &status))
  {
    return status;
  }
  if (!parseNewName(name, canonical))
  {
    return guestFsErrBadName;
  }
  if (length == 0)
  {
    return guestFsErrEmptyFile;
  }
  status = loadDirectory(volume, &directory, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t oldIndex = findEntry(&directory, canonical);
  uint32_t needed =
      (uint32_t)(((uint64_t)length + guestFsAppleBlockSize - 1) / guestFsAppleBlockSize);
  uint32_t position = 0;
  if (oldIndex != noEntry && !overwrite)
  {
    status = guestFsErrExists;
  }
  else if (oldIndex == noEntry && directory.count >= directory.capacity)
  {
    status = guestFsErrDirectoryFull;
  }
  else if (needed > directory.totalBlocks || !findGap(&directory, needed, &position))
  {
    status = guestFsErrNoSpace;
  }
  if (status != guestFsOk)
  {
    freeDirectory(&directory);
    return status;
  }
  chunk = malloc((size_t)chunkBlocks * guestFsAppleBlockSize);
  newRaw = malloc((size_t)directory.dirBlocks * guestFsAppleBlockSize);
  if (chunk == NULL || newRaw == NULL)
  {
    status = guestFsErrIo;
    goto finished;
  }
  //-- Data goes to unreferenced free blocks first; the directory commit below makes it visible.
  for (uint32_t index = 0; index < needed && status == guestFsOk;)
  {
    uint32_t blocksNow = needed - index < chunkBlocks ? needed - index : chunkBlocks;
    uint32_t offset = index * guestFsAppleBlockSize;
    uint32_t have = length - offset < blocksNow * guestFsAppleBlockSize
                        ? length - offset
                        : blocksNow * guestFsAppleBlockSize;
    memset(chunk, 0, (size_t)blocksNow * guestFsAppleBlockSize);
    if (!source(context, chunk, have))
    {
      status = guestFsErrAborted;
      break;
    }
    for (uint32_t inside = 0; inside < blocksNow; ++inside)
    {
      if (!guestFsAppleWriteBlock(&volume->device, position + index + inside,
                                  chunk + (size_t)inside * guestFsAppleBlockSize))
      {
        status = guestFsErrIo;
        break;
      }
    }
    index += blocksNow;
  }
  if (status == guestFsOk && !volume->device.io.flush(volume->device.io.context))
  {
    status = guestFsErrIo;
  }
  if (status == guestFsOk && guestFsCrashPoint("dataWritten"))
  {
    status = guestFsErrAborted;
    volume->needsReopen = true;
  }
  if (status == guestFsOk)
  {
    uint8_t slot[slotBytes];
    uint32_t lastBytes = length - (needed - 1) * guestFsAppleBlockSize;
    memset(slot, 0, sizeof(slot));
    putLe16(slot, position);
    putLe16(slot + 2, position + needed);
    putLe16(slot + 4, kindForNewFile(canonical, attributes));
    slot[6] = (uint8_t)strlen(canonical);
    memcpy(slot + 7, canonical, slot[6]);
    putLe16(slot + 22, lastBytes);
    buildDirectory(&directory, newRaw, oldIndex, slot);
    status = commitDirectory(volume, &directory, newRaw);
  }
finished:
  free(newRaw);
  free(chunk);
  freeDirectory(&directory);
  return status;
}

static guestFsStatus pascalRemove(guestFsVolume* base, uint8_t user, const char* name)
{
  pascalVolume* volume = (pascalVolume*)base;
  pascalDirectory directory;
  guestFsStatus status = guestFsOk;
  if (!opAllowed(volume, user, true, &status))
  {
    return status;
  }
  status = loadDirectory(volume, &directory, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t index = findEntry(&directory, name);
  if (index == noEntry)
  {
    status = guestFsErrNotFound;
  }
  else
  {
    uint8_t* newRaw = malloc((size_t)directory.dirBlocks * guestFsAppleBlockSize);
    if (newRaw == NULL)
    {
      status = guestFsErrIo;
    }
    else
    {
      buildDirectory(&directory, newRaw, index, NULL);
      status = commitDirectory(volume, &directory, newRaw);
      free(newRaw);
    }
  }
  freeDirectory(&directory);
  return status;
}

static guestFsStatus pascalRename(guestFsVolume* base, uint8_t user, const char* name,
                                  const char* newName)
{
  pascalVolume* volume = (pascalVolume*)base;
  pascalDirectory directory;
  char canonical[maxNameLength + 1];
  guestFsStatus status = guestFsOk;
  if (!opAllowed(volume, user, true, &status))
  {
    return status;
  }
  if (!parseNewName(newName, canonical))
  {
    return guestFsErrBadName;
  }
  status = loadDirectory(volume, &directory, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t index = findEntry(&directory, name);
  uint32_t clash = findEntry(&directory, canonical);
  if (index == noEntry)
  {
    status = guestFsErrNotFound;
  }
  else if (clash != noEntry && clash != index)
  {
    status = guestFsErrExists;
  }
  else
  {
    uint8_t* newRaw = malloc((size_t)directory.dirBlocks * guestFsAppleBlockSize);
    if (newRaw == NULL)
    {
      status = guestFsErrIo;
    }
    else
    {
      uint8_t* slot = newRaw + (size_t)slotBytes * (index + 1);
      memcpy(newRaw, directory.raw, (size_t)directory.dirBlocks * guestFsAppleBlockSize);
      memset(slot + 6, 0, 1 + maxNameLength);
      slot[6] = (uint8_t)strlen(canonical);
      memcpy(slot + 7, canonical, slot[6]);
      status = commitDirectory(volume, &directory, newRaw);
      free(newRaw);
    }
  }
  freeDirectory(&directory);
  return status;
}

const guestFsOps guestFsPascalOps = {
    .fsName = "UCSD Pascal",
    .supportsOs = pascalSupportsOs,
    .open = pascalOpen,
    .close = pascalClose,
    .info = pascalInfo,
    .list = pascalList,
    .stat = pascalStat,
    .readAt = pascalReadAt,
    .write = pascalWrite,
    .remove = pascalRemove,
    .rename = pascalRename,
};
