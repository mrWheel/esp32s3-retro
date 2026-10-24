#include "guestFs.h"
#include "guestFsInternal.h"
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//-- ProDOS 8 volumes on 140K / 640K / 800K Apple II images (512-byte blocks), root directory only.
//-- Reference: tools/apple2/diskImageApple2Prodos.py (add_file layout) and
// tools/apple2/extractApple2Files.py
//-- ProDosVolume. The directory and bitmap are re-read from the image for every operation, so the
// volume
//-- object holds no cached view that could go stale. Mutations validate the whole volume first
//(every file
//-- block claimed exactly once and marked used in the bitmap) and fail closed with
// guestFsErrCorrupt.
//-- The ProDOS access byte ("locked") is reported but not enforced: remove, rename and overwrite
// ignore it.
enum
{
  entryBytes = 0x27,
  entriesPerBlock = 13,
  firstDirectoryBlock = 2,
  maxDirectoryBlocks = 32,
  maxNameLength = 15,
  minTotalBlocks = 8,
  blocksPerBitmapBlock = 4096,
  indexEntries = 256,
  masterEntries = 128,
  maxFileBytes = 0xFFFFFF,
  chunkBlocks = 8,
  storageFree = 0,
  storageSeedling = 1,
  storageSapling = 2,
  storageTree = 3,
  storageDirectory = 0xD,
  storageSubdirectoryHeader = 0xE,
  storageVolumeHeader = 0xF,
  accessDefault = 0xC3,
  accessLocked = 0x21,
  accessWrite = 0x02
};

#define noEntry 0xFFFFFFFFu

typedef struct
{
  guestFsVolume base;
  guestFsAppleDevice device;
  bool needsReopen;
  char journalPath[320];
  char journalId[guestFsJournalIdCapacity];
} prodosVolume;

//-- Slot 0 of the directory chain is the volume header, slots 1..capacity are the file entries.
typedef struct
{
  uint8_t* directory;
  uint8_t* bitmap;
  uint8_t* claimed;
  uint32_t directoryBlock[maxDirectoryBlocks];
  uint32_t directoryBlocks;
  uint32_t bitmapStart;
  uint32_t bitmapBlocks;
  uint32_t totalBlocks;
  uint32_t capacity;
  uint32_t activeCount;
  uint32_t freeBlocks;
} prodosView;

typedef struct
{
  uint32_t number;
  bool valid;
  uint8_t data[guestFsAppleBlockSize];
} blockCache;

typedef struct
{
  blockCache master;
  blockCache index;
  uint8_t data[guestFsAppleBlockSize];
} readState;

typedef bool (*blockVisitor)(void* context, uint32_t block);

typedef struct
{
  prodosView* view;
  const char* reason;
} claimContext;

typedef struct
{
  uint8_t* bitmap;
  uint32_t totalBlocks;
} freeContext;

static const struct
{
  uint8_t type;
  const char* label;
} typeLabels[] = {{0x00, "NON"}, {0x01, "BAD"}, {0x04, "TXT"}, {0x06, "BIN"}, {0x0B, "PCD"},
                  {0x0C, "PTX"}, {0x0D, "PDA"}, {0x0F, "DIR"}, {0x19, "ADB"}, {0x1A, "AWP"},
                  {0x1B, "ASP"}, {0xB0, "SRC"}, {0xB3, "S16"}, {0xEF, "PAS"}, {0xF0, "CMD"},
                  {0xFA, "INT"}, {0xFB, "IVR"}, {0xFC, "BAS"}, {0xFD, "VAR"}, {0xFE, "REL"},
                  {0xFF, "SYS"}};

static uint16_t le16(const uint8_t* bytes)
{
  return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

static uint32_t le24(const uint8_t* bytes)
{
  return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16);
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

static bool bitmapIsFree(const uint8_t* bitmap, uint32_t block)
{
  return (bitmap[block >> 3] & (0x80 >> (block & 7))) != 0;
}

static void bitmapSetFree(uint8_t* bitmap, uint32_t block, bool isFree)
{
  if (isFree)
  {
    bitmap[block >> 3] |= (uint8_t)(0x80 >> (block & 7));
  }
  else
  {
    bitmap[block >> 3] &= (uint8_t)~(0x80 >> (block & 7));
  }
}

static uint8_t* slotPointer(uint8_t* directory, uint32_t slot)
{
  return directory + (size_t)(slot / entriesPerBlock) * guestFsAppleBlockSize + 4 +
         (size_t)(slot % entriesPerBlock) * entryBytes;
}

static uint8_t* viewSlot(const prodosView* view, uint32_t slot)
{
  return slotPointer(view->directory, slot);
}

static void freeView(prodosView* view)
{
  free(view->directory);
  free(view->bitmap);
  free(view->claimed);
  memset(view, 0, sizeof(*view));
}

static bool storageIsFile(uint8_t storage)
{
  return storage >= storageSeedling && storage <= storageTree;
}

static uint32_t blocksForBytes(uint32_t bytes)
{
  return (bytes + guestFsAppleBlockSize - 1) / guestFsAppleBlockSize;
}

//-- A readable file: seedling / sapling / tree whose end of file fits what its index structure can
// address.
static bool fileUsable(const uint8_t* entry)
{
  uint8_t storage = entry[0] >> 4;
  uint32_t needed = blocksForBytes(le24(entry + 0x15));
  uint32_t key = le16(entry + 0x11);
  if (!storageIsFile(storage) || key == 0)
  {
    return false;
  }
  switch (storage)
  {
  case storageSeedling:
    return needed <= 1;
  case storageSapling:
    return needed <= indexEntries;
  default:
    return needed <= (uint32_t)indexEntries * masterEntries;
  }
}

static bool nameMatches(const uint8_t* entry, const char* canonical, size_t length)
{
  if ((size_t)(entry[0] & 0x0F) != length)
  {
    return false;
  }
  for (size_t index = 0; index < length; ++index)
  {
    if (toupper(entry[1 + index]) != canonical[index])
    {
      return false;
    }
  }
  return true;
}

static uint32_t indexPointer(const uint8_t* index, uint32_t position)
{
  return (uint32_t)index[position] | ((uint32_t)index[indexEntries + position] << 8);
}

//-- Visits every block a file owns (key, index and data blocks; sparse zero pointers skipped).
//-- Returns guestFsErrCorrupt when the visitor refuses a block or a pointer lies outside the
// volume.
static guestFsStatus walkFile(const prodosVolume* volume, uint32_t totalBlocks,
                              const uint8_t* entry, blockVisitor visit, void* context)
{
  uint8_t storage = entry[0] >> 4;
  uint32_t key = le16(entry + 0x11);
  if (!visit(context, key) || key >= totalBlocks)
  {
    return guestFsErrCorrupt;
  }
  if (storage == storageSeedling)
  {
    return guestFsOk;
  }
  uint8_t index[guestFsAppleBlockSize];
  if (!guestFsAppleReadBlock(&volume->device, key, index))
  {
    return guestFsErrIo;
  }
  if (storage == storageSapling)
  {
    for (uint32_t position = 0; position < indexEntries; ++position)
    {
      uint32_t pointer = indexPointer(index, position);
      if (pointer != 0 && (!visit(context, pointer) || pointer >= totalBlocks))
      {
        return guestFsErrCorrupt;
      }
    }
    return guestFsOk;
  }
  uint8_t master[guestFsAppleBlockSize];
  memcpy(master, index, sizeof(master));
  for (uint32_t slot = 0; slot < masterEntries; ++slot)
  {
    uint32_t indexBlock = indexPointer(master, slot);
    if (indexBlock == 0)
    {
      continue;
    }
    if (!visit(context, indexBlock) || indexBlock >= totalBlocks)
    {
      return guestFsErrCorrupt;
    }
    if (!guestFsAppleReadBlock(&volume->device, indexBlock, index))
    {
      return guestFsErrIo;
    }
    for (uint32_t position = 0; position < indexEntries; ++position)
    {
      uint32_t pointer = indexPointer(index, position);
      if (pointer != 0 && (!visit(context, pointer) || pointer >= totalBlocks))
      {
        return guestFsErrCorrupt;
      }
    }
  }
  return guestFsOk;
}

//-- Visits the blocks of a subdirectory chain (only used to make the claim map complete).
static guestFsStatus walkDirectoryChain(const prodosVolume* volume, uint32_t totalBlocks,
                                        uint32_t key, blockVisitor visit, void* context)
{
  uint8_t buffer[guestFsAppleBlockSize];
  uint32_t block = key;
  while (block != 0)
  {
    if (!visit(context, block) || block >= totalBlocks)
    {
      return guestFsErrCorrupt;
    }
    if (!guestFsAppleReadBlock(&volume->device, block, buffer))
    {
      return guestFsErrIo;
    }
    block = le16(buffer + 2);
  }
  return guestFsOk;
}

static bool claimVisitor(void* context, uint32_t block)
{
  claimContext* claim = context;
  prodosView* view = claim->view;
  if (block >= view->totalBlocks)
  {
    claim->reason = "points outside the volume";
    return false;
  }
  if ((view->claimed[block >> 3] & (0x80 >> (block & 7))) != 0)
  {
    claim->reason = "shares a block with another file or the directory";
    return false;
  }
  if (bitmapIsFree(view->bitmap, block))
  {
    claim->reason = "uses a block that the volume bitmap marks as free";
    return false;
  }
  view->claimed[block >> 3] |= (uint8_t)(0x80 >> (block & 7));
  return true;
}

static bool freeVisitor(void* context, uint32_t block)
{
  freeContext* release = context;
  if (block >= release->totalBlocks)
  {
    return false;
  }
  bitmapSetFree(release->bitmap, block, true);
  return true;
}

static bool nameCharactersValid(const uint8_t* entry)
{
  uint32_t length = entry[0] & 0x0F;
  if (length < 1 || length > maxNameLength)
  {
    return false;
  }
  for (uint32_t index = 0; index < length; ++index)
  {
    if (entry[1 + index] < 0x21 || entry[1 + index] > 0x7E)
    {
      return false;
    }
  }
  return true;
}

//-- Full validation used before every mutation.
static guestFsStatus validateClaims(const prodosVolume* volume, prodosView* view, char* detail,
                                    size_t detailCapacity)
{
  claimContext claim = {view, NULL};
  view->claimed = calloc((view->totalBlocks + 7) / 8, 1);
  if (view->claimed == NULL)
  {
    return guestFsErrIo;
  }
  uint32_t reserved[2 + maxDirectoryBlocks];
  uint32_t reservedCount = 0;
  reserved[reservedCount++] = 0;
  reserved[reservedCount++] = 1;
  for (uint32_t index = 0; index < view->directoryBlocks; ++index)
  {
    reserved[reservedCount++] = view->directoryBlock[index];
  }
  for (uint32_t index = 0; index < reservedCount; ++index)
  {
    if (!claimVisitor(&claim, reserved[index]))
    {
      setDetail(detail, detailCapacity, "ProDOS block %u (boot or directory) %s",
                (unsigned)reserved[index], claim.reason);
      return guestFsErrCorrupt;
    }
  }
  for (uint32_t index = 0; index < view->bitmapBlocks; ++index)
  {
    if (!claimVisitor(&claim, view->bitmapStart + index))
    {
      setDetail(detail, detailCapacity, "ProDOS bitmap block %u %s",
                (unsigned)(view->bitmapStart + index), claim.reason);
      return guestFsErrCorrupt;
    }
  }
  for (uint32_t slot = 1; slot <= view->capacity; ++slot)
  {
    const uint8_t* entry = viewSlot(view, slot);
    uint8_t storage = entry[0] >> 4;
    guestFsStatus status = guestFsOk;
    if (storageIsFile(storage))
    {
      if (!fileUsable(entry))
      {
        continue;
      }
      status = walkFile(volume, view->totalBlocks, entry, claimVisitor, &claim);
    }
    else if (storage == storageDirectory)
    {
      status =
          walkDirectoryChain(volume, view->totalBlocks, le16(entry + 0x11), claimVisitor, &claim);
    }
    if (status == guestFsErrCorrupt)
    {
      char name[maxNameLength + 1];
      memcpy(name, entry + 1, entry[0] & 0x0F);
      name[entry[0] & 0x0F] = '\0';
      setDetail(detail, detailCapacity, "ProDOS entry %s %s", name,
                claim.reason != NULL ? claim.reason : "has an unreadable index block");
    }
    if (status != guestFsOk)
    {
      return status;
    }
  }
  return guestFsOk;
}

//-- Reads and validates the root directory and the volume bitmap. guestFsErrUnsupported (detail
// untouched)
//-- means "not a ProDOS volume"; guestFsErrCorrupt means it is one but cannot be trusted. full adds
// the
//-- block ownership check required before a mutation.
static guestFsStatus loadView(const prodosVolume* volume, prodosView* view, bool full, char* detail,
                              size_t detailCapacity)
{
  const guestFsAppleDevice* device = &volume->device;
  uint32_t deviceBlocks = guestFsAppleBlockCount(device);
  guestFsStatus status = guestFsOk;
  memset(view, 0, sizeof(*view));
  view->directory = malloc(guestFsAppleBlockSize);
  if (view->directory == NULL)
  {
    return guestFsErrIo;
  }
  if (!guestFsAppleReadBlock(device, firstDirectoryBlock, view->directory))
  {
    freeView(view);
    return guestFsErrIo;
  }
  const uint8_t* header = view->directory + 4;
  if (le16(view->directory) != 0 || (header[0] >> 4) != storageVolumeHeader ||
      (header[0] & 0x0F) < 1 || (header[0] & 0x0F) > maxNameLength || header[0x1F] != entryBytes ||
      header[0x20] != entriesPerBlock)
  {
    freeView(view);
    return guestFsErrUnsupported;
  }
  view->bitmapStart = le16(header + 0x23);
  view->totalBlocks = le16(header + 0x25);
  view->bitmapBlocks = (view->totalBlocks + blocksPerBitmapBlock - 1) / blocksPerBitmapBlock;
  if (view->totalBlocks < minTotalBlocks || view->totalBlocks > deviceBlocks)
  {
    setDetail(detail, detailCapacity, "ProDOS volume claims %u blocks but the image holds %u",
              (unsigned)view->totalBlocks, (unsigned)deviceBlocks);
    status = guestFsErrCorrupt;
  }
  else if (view->bitmapStart < 3 || view->bitmapStart + view->bitmapBlocks > view->totalBlocks)
  {
    setDetail(detail, detailCapacity, "ProDOS bitmap block %u is outside the volume",
              (unsigned)view->bitmapStart);
    status = guestFsErrCorrupt;
  }
  view->directoryBlock[0] = firstDirectoryBlock;
  view->directoryBlocks = 1;
  while (status == guestFsOk)
  {
    uint32_t next =
        le16(view->directory + (size_t)(view->directoryBlocks - 1) * guestFsAppleBlockSize + 2);
    if (next == 0)
    {
      break;
    }
    bool seen = false;
    for (uint32_t index = 0; index < view->directoryBlocks; ++index)
    {
      seen = seen || view->directoryBlock[index] == next;
    }
    if (seen)
    {
      setDetail(detail, detailCapacity, "ProDOS directory chain loops back to block %u",
                (unsigned)next);
      status = guestFsErrCorrupt;
    }
    else if (next < 3 || next >= view->totalBlocks || view->directoryBlocks >= maxDirectoryBlocks)
    {
      setDetail(detail, detailCapacity, "ProDOS directory chain pointer %u is invalid",
                (unsigned)next);
      status = guestFsErrCorrupt;
    }
    else
    {
      uint8_t* grown =
          realloc(view->directory, (size_t)(view->directoryBlocks + 1) * guestFsAppleBlockSize);
      if (grown == NULL)
      {
        status = guestFsErrIo;
        break;
      }
      view->directory = grown;
      if (!guestFsAppleReadBlock(device, next,
                                 view->directory +
                                     (size_t)view->directoryBlocks * guestFsAppleBlockSize))
      {
        status = guestFsErrIo;
        break;
      }
      view->directoryBlock[view->directoryBlocks++] = next;
    }
  }
  if (status == guestFsOk)
  {
    view->capacity = view->directoryBlocks * entriesPerBlock - 1;
    view->bitmap = malloc((size_t)view->bitmapBlocks * guestFsAppleBlockSize);
    if (view->bitmap == NULL)
    {
      status = guestFsErrIo;
    }
  }
  for (uint32_t index = 0; status == guestFsOk && index < view->bitmapBlocks; ++index)
  {
    if (!guestFsAppleReadBlock(device, view->bitmapStart + index,
                               view->bitmap + (size_t)index * guestFsAppleBlockSize))
    {
      status = guestFsErrIo;
    }
  }
  for (uint32_t block = 0; status == guestFsOk && block < view->totalBlocks; ++block)
  {
    view->freeBlocks += bitmapIsFree(view->bitmap, block) ? 1 : 0;
  }
  for (uint32_t slot = 1; status == guestFsOk && slot <= view->capacity; ++slot)
  {
    const uint8_t* entry = viewSlot(view, slot);
    uint8_t storage = entry[0] >> 4;
    if (storage == storageFree)
    {
      continue;
    }
    if (storage == storageSubdirectoryHeader || storage == storageVolumeHeader ||
        !nameCharactersValid(entry))
    {
      setDetail(detail, detailCapacity, "ProDOS directory slot %u holds an invalid entry",
                (unsigned)slot);
      status = guestFsErrCorrupt;
      break;
    }
    ++view->activeCount;
    char upper[maxNameLength];
    uint32_t length = entry[0] & 0x0F;
    for (uint32_t character = 0; character < length; ++character)
    {
      upper[character] = (char)toupper(entry[1 + character]);
    }
    for (uint32_t other = 1; other < slot; ++other)
    {
      const uint8_t* previous = viewSlot(view, other);
      if ((previous[0] >> 4) != storageFree && nameMatches(previous, upper, length))
      {
        setDetail(detail, detailCapacity, "ProDOS directory slots %u and %u have the same name",
                  (unsigned)other, (unsigned)slot);
        status = guestFsErrCorrupt;
        break;
      }
    }
  }
  if (status == guestFsOk && full)
  {
    status = validateClaims(volume, view, detail, detailCapacity);
  }
  if (status != guestFsOk)
  {
    freeView(view);
  }
  return status;
}

//-- Lookup is lenient (any listed name, case-insensitive); creating names are checked by
// parseNewName.
static uint32_t findSlot(const prodosView* view, const char* name)
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
  for (uint32_t slot = 1; slot <= view->capacity; ++slot)
  {
    const uint8_t* entry = viewSlot(view, slot);
    if ((entry[0] >> 4) != storageFree && nameMatches(entry, upper, length))
    {
      return slot;
    }
  }
  return noEntry;
}

static uint32_t findFreeSlot(const prodosView* view)
{
  for (uint32_t slot = 1; slot <= view->capacity; ++slot)
  {
    if ((viewSlot(view, slot)[0] >> 4) == storageFree)
    {
      return slot;
    }
  }
  return noEntry;
}

//-- Same rule as diskImageApple2Prodos: letter first, then letters, digits and '.'.
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
    bool letter = character >= 'A' && character <= 'Z';
    bool other = (character >= '0' && character <= '9') || character == '.';
    if ((index == 0 && !letter) || (!letter && !other))
    {
      return false;
    }
    canonical[index] = (char)character;
  }
  canonical[length] = '\0';
  return true;
}

static uint8_t pascalKindOfType(uint8_t type)
{
  return type == 0x0B ? 2 : (type == 0x0C ? 3 : 5);
}

static void fillEntry(const uint8_t* entry, guestFsEntry* result)
{
  uint8_t storage = entry[0] >> 4;
  uint8_t type = entry[0x10];
  memset(result, 0, sizeof(*result));
  memcpy(result->name, entry + 1, entry[0] & 0x0F);
  result->size = le24(entry + 0x15);
  result->allocatedBytes = (uint32_t)le16(entry + 0x13) * guestFsAppleBlockSize;
  result->readOnlyAttribute = (entry[0x1E] & accessWrite) == 0;
  result->directory = storage == storageDirectory;
  result->damaged = !result->directory && !fileUsable(entry);
  snprintf(result->typeLabel, sizeof(result->typeLabel), "$%02X", (unsigned)type);
  for (size_t index = 0; index < sizeof(typeLabels) / sizeof(typeLabels[0]); ++index)
  {
    if (typeLabels[index].type == type)
    {
      snprintf(result->typeLabel, sizeof(result->typeLabel), "%s", typeLabels[index].label);
    }
  }
  if (result->directory)
  {
    snprintf(result->typeLabel, sizeof(result->typeLabel), "DIR");
  }
  result->hasAttributes = true;
  result->attributes.prodosType = type;
  result->attributes.auxType = le16(entry + 0x1F);
  result->attributes.pascalKind = pascalKindOfType(type);
  result->attributes.locked = result->readOnlyAttribute;
}

static bool opAllowed(const prodosVolume* volume, uint8_t user, bool writing, guestFsStatus* status)
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

static bool prodosSupportsOs(const char* osId)
{
  return guestFsAppleSupportsOs(osId);
}

static void prodosClose(guestFsVolume* base)
{
  free(base);
}

static guestFsStatus prodosOpen(const guestFsIo* io, const char* osId, const char* journalPath,
                                const char* journalId, guestFsVolume** result, char* detail,
                                size_t detailCapacity)
{
  (void)osId;
  prodosVolume* volume = NULL;
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
  prodosView view;
  if (status == guestFsOk)
  {
    status = loadView(volume, &view, false, detail, detailCapacity);
  }
  if (status != guestFsOk)
  {
    free(volume);
    return status;
  }
  freeView(&view);
  volume->base.ops = &guestFsProdosOps;
  strcpy(volume->journalPath, journalPath);
  strcpy(volume->journalId, journalId);
  *result = &volume->base;
  return guestFsOk;
}

static void prodosInfo(const guestFsVolume* base, guestFsInfo* info)
{
  const prodosVolume* volume = (const prodosVolume*)base;
  prodosView view;
  memset(info, 0, sizeof(*info));
  info->fsName = "ProDOS";
  info->blockSize = guestFsAppleBlockSize;
  info->nameRule = "prodos";
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
  if (loadView(volume, &view, false, NULL, 0) != guestFsOk)
  {
    return;
  }
  info->totalBlocks = view.totalBlocks;
  info->freeBlocks = view.freeBlocks;
  info->directoryEntries = view.capacity;
  info->freeDirectoryEntries = view.capacity - view.activeCount;
  freeView(&view);
}

static guestFsStatus prodosList(guestFsVolume* base, guestFsListFn callback, void* context)
{
  prodosVolume* volume = (prodosVolume*)base;
  prodosView view;
  guestFsStatus status = guestFsOk;
  if (!opAllowed(volume, 0, false, &status))
  {
    return status;
  }
  status = loadView(volume, &view, false, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  for (uint32_t slot = 1; slot <= view.capacity; ++slot)
  {
    const uint8_t* entry = viewSlot(&view, slot);
    if ((entry[0] >> 4) == storageFree)
    {
      continue;
    }
    guestFsEntry result;
    fillEntry(entry, &result);
    if (!callback(context, &result))
    {
      break;
    }
  }
  freeView(&view);
  return status;
}

static guestFsStatus prodosStat(guestFsVolume* base, uint8_t user, const char* name,
                                guestFsEntry* entry)
{
  prodosVolume* volume = (prodosVolume*)base;
  prodosView view;
  guestFsStatus status = guestFsOk;
  if (!opAllowed(volume, user, false, &status))
  {
    return status;
  }
  status = loadView(volume, &view, false, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t slot = findSlot(&view, name);
  if (slot == noEntry)
  {
    status = guestFsErrNotFound;
  }
  else
  {
    fillEntry(viewSlot(&view, slot), entry);
  }
  freeView(&view);
  return status;
}

static bool cachedRead(const prodosVolume* volume, blockCache* cache, uint32_t number)
{
  if (!cache->valid || cache->number != number)
  {
    cache->valid = false;
    if (!guestFsAppleReadBlock(&volume->device, number, cache->data))
    {
      return false;
    }
    cache->number = number;
    cache->valid = true;
  }
  return true;
}

//-- Block number holding data block `position` of a file (0 = sparse hole).
static guestFsStatus dataBlockOf(const prodosVolume* volume, uint32_t totalBlocks,
                                 const uint8_t* entry, uint32_t position, readState* state,
                                 uint32_t* block)
{
  uint8_t storage = entry[0] >> 4;
  uint32_t key = le16(entry + 0x11);
  uint32_t pointer = 0;
  if (key >= totalBlocks)
  {
    return guestFsErrDamagedFile;
  }
  if (storage == storageSeedling)
  {
    pointer = key;
  }
  else if (storage == storageSapling)
  {
    if (!cachedRead(volume, &state->index, key))
    {
      return guestFsErrIo;
    }
    pointer = indexPointer(state->index.data, position);
  }
  else
  {
    uint32_t slot = position / indexEntries;
    uint32_t inside = position % indexEntries;
    if (!cachedRead(volume, &state->master, key))
    {
      return guestFsErrIo;
    }
    uint32_t indexBlock = indexPointer(state->master.data, slot);
    if (indexBlock != 0)
    {
      if (indexBlock >= totalBlocks)
      {
        return guestFsErrDamagedFile;
      }
      if (!cachedRead(volume, &state->index, indexBlock))
      {
        return guestFsErrIo;
      }
      pointer = indexPointer(state->index.data, inside);
    }
  }
  if (pointer >= totalBlocks)
  {
    return guestFsErrDamagedFile;
  }
  *block = pointer;
  return guestFsOk;
}

static guestFsStatus prodosReadAt(guestFsVolume* base, uint8_t user, const char* name,
                                  uint32_t offset, void* buffer, size_t length, size_t* got)
{
  prodosVolume* volume = (prodosVolume*)base;
  prodosView view;
  guestFsStatus status = guestFsOk;
  *got = 0;
  if (!opAllowed(volume, user, false, &status))
  {
    return status;
  }
  status = loadView(volume, &view, false, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t slot = findSlot(&view, name);
  uint8_t entry[entryBytes];
  readState* state = NULL;
  if (slot == noEntry)
  {
    status = guestFsErrNotFound;
  }
  else
  {
    memcpy(entry, viewSlot(&view, slot), entryBytes);
    if ((entry[0] >> 4) == storageDirectory)
    {
      status = guestFsErrUnsupported;
    }
    else if (!fileUsable(entry))
    {
      status = guestFsErrDamagedFile;
    }
  }
  if (status == guestFsOk)
  {
    state = calloc(1, sizeof(*state));
    if (state == NULL)
    {
      status = guestFsErrIo;
    }
  }
  if (status == guestFsOk)
  {
    uint32_t size = le24(entry + 0x15);
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
        uint32_t block = 0;
        status = dataBlockOf(volume, view.totalBlocks, entry, position / guestFsAppleBlockSize,
                             state, &block);
        if (status != guestFsOk)
        {
          break;
        }
        if (block == 0)
        {
          memset(state->data, 0, sizeof(state->data));
        }
        else if (!guestFsAppleReadBlock(&volume->device, block, state->data))
        {
          status = guestFsErrIo;
          break;
        }
        memcpy(target + *got, state->data + inside, piece);
        *got += piece;
        position += (uint32_t)piece;
        remaining -= piece;
      }
    }
  }
  free(state);
  freeView(&view);
  return status;
}

//-- Journaled commit of every directory and bitmap block that differs from the view just read.
static guestFsStatus commitView(prodosVolume* volume, const prodosView* view,
                                const uint8_t* newDirectory, const uint8_t* newBitmap)
{
  guestFsRangeChange* changes =
      calloc(2U * (view->directoryBlocks + view->bitmapBlocks), sizeof(*changes));
  size_t count = 0;
  if (changes == NULL)
  {
    return guestFsErrIo;
  }
  for (uint32_t block = 0; block < view->directoryBlocks; ++block)
  {
    size_t offset = (size_t)block * guestFsAppleBlockSize;
    if (memcmp(view->directory + offset, newDirectory + offset, guestFsAppleBlockSize) != 0)
    {
      count += guestFsAppleBlockChanges(&volume->device, view->directoryBlock[block],
                                        newDirectory + offset, &changes[count]);
    }
  }
  for (uint32_t block = 0; block < view->bitmapBlocks; ++block)
  {
    size_t offset = (size_t)block * guestFsAppleBlockSize;
    if (memcmp(view->bitmap + offset, newBitmap + offset, guestFsAppleBlockSize) != 0)
    {
      count += guestFsAppleBlockChanges(&volume->device, view->bitmapStart + block,
                                        newBitmap + offset, &changes[count]);
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

//-- File type rule of diskImageApple2Prodos (suffix based) when the caller gives no attributes.
static void typeForNewFile(const char* canonical, const guestFsAttributes* attributes,
                           uint8_t* type, uint16_t* aux, bool* locked)
{
  static const struct
  {
    const char* suffix;
    uint8_t type;
    uint16_t aux;
  } suffixes[] = {
      {".BAS", 0xFC, 0x0801}, {".TXT", 0x04, 0}, {".BIN", 0x06, 0}, {".SYS", 0xFF, 0x2000}};
  *type = 0x00;
  *aux = 0;
  *locked = false;
  if (attributes != NULL)
  {
    *type = (uint8_t)attributes->prodosType;
    *aux = attributes->auxType;
    *locked = attributes->locked;
    return;
  }
  const char* dot = strrchr(canonical, '.');
  for (size_t index = 0; dot != NULL && index < sizeof(suffixes) / sizeof(suffixes[0]); ++index)
  {
    if (strcmp(dot, suffixes[index].suffix) == 0)
    {
      *type = suffixes[index].type;
      *aux = suffixes[index].aux;
    }
  }
}

static void buildEntry(uint8_t* entry, const char* canonical, uint8_t storage, uint8_t type,
                       uint16_t aux, bool locked, uint32_t key, uint32_t blocksUsed,
                       uint32_t length)
{
  size_t nameLength = strlen(canonical);
  memset(entry, 0, entryBytes);
  entry[0] = (uint8_t)((storage << 4) | nameLength);
  memcpy(entry + 1, canonical, nameLength);
  entry[0x10] = type;
  putLe16(entry + 0x11, key);
  putLe16(entry + 0x13, blocksUsed);
  entry[0x15] = (uint8_t)(length & 0xFF);
  entry[0x16] = (uint8_t)((length >> 8) & 0xFF);
  entry[0x17] = (uint8_t)((length >> 16) & 0xFF);
  entry[0x1E] = locked ? accessLocked : accessDefault;
  putLe16(entry + 0x1F, aux);
  putLe16(entry + 0x25, firstDirectoryBlock);
}

static void storeIndex(uint8_t* buffer, const uint16_t* pointers, uint32_t count)
{
  memset(buffer, 0, guestFsAppleBlockSize);
  for (uint32_t position = 0; position < count; ++position)
  {
    buffer[position] = (uint8_t)(pointers[position] & 0xFF);
    buffer[indexEntries + position] = (uint8_t)(pointers[position] >> 8);
  }
}

static void adjustFileCount(uint8_t* directory, int delta)
{
  uint8_t* header = slotPointer(directory, 0);
  int count = (int)le16(header + 0x21) + delta;
  putLe16(header + 0x21, (uint32_t)(count < 0 ? 0 : count));
}

static guestFsStatus prodosWrite(guestFsVolume* base, uint8_t user, const char* name,
                                 uint32_t length, guestFsSourceFn source, void* context,
                                 bool overwrite, const guestFsAttributes* attributes)
{
  prodosVolume* volume = (prodosVolume*)base;
  prodosView view;
  char canonical[maxNameLength + 1];
  uint16_t* allocated = NULL;
  uint8_t* chunk = NULL;
  uint8_t* newDirectory = NULL;
  uint8_t* newBitmap = NULL;
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
  if (attributes != NULL && (attributes->prodosType > 0xFF || attributes->prodosType == 0x0F))
  {
    return guestFsErrInvalidArgument;
  }
  status = loadView(volume, &view, true, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t oldSlot = findSlot(&view, canonical);
  uint32_t targetSlot = oldSlot;
  if (oldSlot != noEntry)
  {
    const uint8_t* oldEntry = viewSlot(&view, oldSlot);
    if (!overwrite)
    {
      status = guestFsErrExists;
    }
    else if ((oldEntry[0] >> 4) == storageDirectory)
    {
      status = guestFsErrUnsupported;
    }
    else if (!fileUsable(oldEntry))
    {
      status = guestFsErrDamagedFile;
    }
  }
  else
  {
    targetSlot = findFreeSlot(&view);
    if (targetSlot == noEntry)
    {
      status = guestFsErrDirectoryFull;
    }
  }
  uint32_t dataBlocks = blocksForBytes(length);
  uint8_t storage = storageSeedling;
  uint32_t indexCount = 0;
  if (dataBlocks > 1 && dataBlocks <= indexEntries)
  {
    storage = storageSapling;
    indexCount = 1;
  }
  else if (dataBlocks > indexEntries)
  {
    storage = storageTree;
    indexCount = (dataBlocks + indexEntries - 1) / indexEntries + 1;
  }
  uint32_t totalNeeded = indexCount + dataBlocks;
  if (status == guestFsOk &&
      (length > maxFileBytes || dataBlocks > (uint32_t)indexEntries * masterEntries ||
       totalNeeded > view.freeBlocks))
  {
    status = guestFsErrNoSpace;
  }
  if (status != guestFsOk)
  {
    freeView(&view);
    return status;
  }
  allocated = malloc((size_t)totalNeeded * sizeof(uint16_t));
  chunk = malloc((size_t)chunkBlocks * guestFsAppleBlockSize);
  newDirectory = malloc((size_t)view.directoryBlocks * guestFsAppleBlockSize);
  newBitmap = malloc((size_t)view.bitmapBlocks * guestFsAppleBlockSize);
  if (allocated == NULL || chunk == NULL || newDirectory == NULL || newBitmap == NULL)
  {
    status = guestFsErrIo;
    goto finished;
  }
  //-- Lowest free blocks first, index blocks before data blocks, exactly like Python add_file.
  uint32_t found = 0;
  memcpy(newBitmap, view.bitmap, (size_t)view.bitmapBlocks * guestFsAppleBlockSize);
  for (uint32_t block = 0; block < view.totalBlocks && found < totalNeeded; ++block)
  {
    if (bitmapIsFree(view.bitmap, block))
    {
      allocated[found++] = (uint16_t)block;
      bitmapSetFree(newBitmap, block, false);
    }
  }
  if (oldSlot != noEntry)
  {
    freeContext release = {newBitmap, view.totalBlocks};
    status = walkFile(volume, view.totalBlocks, viewSlot(&view, oldSlot), freeVisitor, &release);
    if (status != guestFsOk)
    {
      goto finished;
    }
  }
  //-- Data goes to unreferenced free blocks first; the commit below makes it visible.
  const uint16_t* dataList = allocated + indexCount;
  for (uint32_t index = 0; index < dataBlocks && status == guestFsOk;)
  {
    uint32_t blocksNow = dataBlocks - index < chunkBlocks ? dataBlocks - index : chunkBlocks;
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
      if (!guestFsAppleWriteBlock(&volume->device, dataList[index + inside],
                                  chunk + (size_t)inside * guestFsAppleBlockSize))
      {
        status = guestFsErrIo;
        break;
      }
    }
    index += blocksNow;
  }
  uint8_t indexBuffer[guestFsAppleBlockSize];
  if (status == guestFsOk && storage == storageSapling)
  {
    storeIndex(indexBuffer, dataList, dataBlocks);
    if (!guestFsAppleWriteBlock(&volume->device, allocated[0], indexBuffer))
    {
      status = guestFsErrIo;
    }
  }
  else if (status == guestFsOk && storage == storageTree)
  {
    storeIndex(indexBuffer, allocated + 1, indexCount - 1);
    if (!guestFsAppleWriteBlock(&volume->device, allocated[0], indexBuffer))
    {
      status = guestFsErrIo;
    }
    for (uint32_t position = 0; status == guestFsOk && position + 1 < indexCount; ++position)
    {
      uint32_t first = position * indexEntries;
      uint32_t count = dataBlocks - first < indexEntries ? dataBlocks - first : indexEntries;
      storeIndex(indexBuffer, dataList + first, count);
      if (!guestFsAppleWriteBlock(&volume->device, allocated[1 + position], indexBuffer))
      {
        status = guestFsErrIo;
      }
    }
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
    uint8_t entry[entryBytes];
    uint8_t type;
    uint16_t aux;
    bool locked;
    typeForNewFile(canonical, attributes, &type, &aux, &locked);
    buildEntry(entry, canonical, storage, type, aux, locked, allocated[0], totalNeeded, length);
    memcpy(newDirectory, view.directory, (size_t)view.directoryBlocks * guestFsAppleBlockSize);
    memcpy(slotPointer(newDirectory, targetSlot), entry, entryBytes);
    if (oldSlot == noEntry)
    {
      adjustFileCount(newDirectory, 1);
    }
    status = commitView(volume, &view, newDirectory, newBitmap);
  }
finished:
  free(newBitmap);
  free(newDirectory);
  free(chunk);
  free(allocated);
  freeView(&view);
  return status;
}

static guestFsStatus prodosRemove(guestFsVolume* base, uint8_t user, const char* name)
{
  prodosVolume* volume = (prodosVolume*)base;
  prodosView view;
  guestFsStatus status = guestFsOk;
  if (!opAllowed(volume, user, true, &status))
  {
    return status;
  }
  status = loadView(volume, &view, true, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t slot = findSlot(&view, name);
  if (slot == noEntry)
  {
    status = guestFsErrNotFound;
  }
  else if ((viewSlot(&view, slot)[0] >> 4) == storageDirectory)
  {
    status = guestFsErrUnsupported;
  }
  else if (!fileUsable(viewSlot(&view, slot)))
  {
    status = guestFsErrDamagedFile;
  }
  else
  {
    uint8_t* newDirectory = malloc((size_t)view.directoryBlocks * guestFsAppleBlockSize);
    uint8_t* newBitmap = malloc((size_t)view.bitmapBlocks * guestFsAppleBlockSize);
    if (newDirectory == NULL || newBitmap == NULL)
    {
      status = guestFsErrIo;
    }
    else
    {
      freeContext release = {newBitmap, view.totalBlocks};
      memcpy(newBitmap, view.bitmap, (size_t)view.bitmapBlocks * guestFsAppleBlockSize);
      status = walkFile(volume, view.totalBlocks, viewSlot(&view, slot), freeVisitor, &release);
      if (status == guestFsOk)
      {
        memcpy(newDirectory, view.directory, (size_t)view.directoryBlocks * guestFsAppleBlockSize);
        slotPointer(newDirectory, slot)[0] = 0;
        adjustFileCount(newDirectory, -1);
        status = commitView(volume, &view, newDirectory, newBitmap);
      }
    }
    free(newBitmap);
    free(newDirectory);
  }
  freeView(&view);
  return status;
}

static guestFsStatus prodosRename(guestFsVolume* base, uint8_t user, const char* name,
                                  const char* newName)
{
  prodosVolume* volume = (prodosVolume*)base;
  prodosView view;
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
  status = loadView(volume, &view, true, NULL, 0);
  if (status != guestFsOk)
  {
    return status;
  }
  uint32_t slot = findSlot(&view, name);
  uint32_t clash = findSlot(&view, canonical);
  if (slot == noEntry)
  {
    status = guestFsErrNotFound;
  }
  else if ((viewSlot(&view, slot)[0] >> 4) == storageDirectory)
  {
    status = guestFsErrUnsupported;
  }
  else if (clash != noEntry && clash != slot)
  {
    status = guestFsErrExists;
  }
  else
  {
    uint8_t* newDirectory = malloc((size_t)view.directoryBlocks * guestFsAppleBlockSize);
    if (newDirectory == NULL)
    {
      status = guestFsErrIo;
    }
    else
    {
      memcpy(newDirectory, view.directory, (size_t)view.directoryBlocks * guestFsAppleBlockSize);
      uint8_t* entry = slotPointer(newDirectory, slot);
      size_t nameLength = strlen(canonical);
      entry[0] = (uint8_t)((entry[0] & 0xF0) | nameLength);
      memset(entry + 1, 0, maxNameLength);
      memcpy(entry + 1, canonical, nameLength);
      status = commitView(volume, &view, newDirectory, view.bitmap);
      free(newDirectory);
    }
  }
  freeView(&view);
  return status;
}

const guestFsOps guestFsProdosOps = {
    .fsName = "ProDOS",
    .supportsOs = prodosSupportsOs,
    .open = prodosOpen,
    .close = prodosClose,
    .info = prodosInfo,
    .list = prodosList,
    .stat = prodosStat,
    .readAt = prodosReadAt,
    .write = prodosWrite,
    .remove = prodosRemove,
    .rename = prodosRename,
};
