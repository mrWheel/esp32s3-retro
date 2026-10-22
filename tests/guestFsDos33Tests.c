#include "guestFs.h"
#include "guestFsInternal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define check(condition)                                                                           \
  do                                                                                               \
  {                                                                                                \
    if (!(condition))                                                                              \
    {                                                                                              \
      fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);                 \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)

static const char* fixtureDirectory;
static const char* outputDirectory;
static const char* projectRoot;
static int testsRun = 0;

enum
{
  maxFixtureFiles = 24,
  maxHoles = 8,
  sectorBytes = 256,
  catalogSlots = 105
};

//-- Position of a DOS sector in a DOS-order image, and of a catalog slot inside it.
#define sectorAt(track, sector) (((track) * 16 + (sector)) * 256)
#define slotAt(catalogSector, slot) (sectorAt(17, (catalogSector)) + 0x0B + 35 * (slot))

//-- File-backed guestFsIo with an operation budget to simulate power loss.
typedef struct
{
  FILE* file;
  uint64_t size;
  long budget;
  long operations;
  bool readOnly;
} testIo;

static bool testRead(void* context, uint64_t offset, void* buffer, size_t length)
{
  testIo* io = context;
  return offset + length <= io->size && fseek(io->file, (long)offset, SEEK_SET) == 0 &&
         fread(buffer, 1, length, io->file) == length;
}

static bool testWrite(void* context, uint64_t offset, const void* buffer, size_t length)
{
  testIo* io = context;
  if (io->readOnly || (io->budget >= 0 && io->operations >= io->budget))
  {
    return false;
  }
  ++io->operations;
  return offset + length <= io->size && fseek(io->file, (long)offset, SEEK_SET) == 0 &&
         fwrite(buffer, 1, length, io->file) == length;
}

static bool testFlush(void* context)
{
  testIo* io = context;
  if (io->budget >= 0 && io->operations >= io->budget)
  {
    return false;
  }
  ++io->operations;
  return fflush(io->file) == 0;
}

static void ioOpen(testIo* io, guestFsIo* guest, const char* path, bool readOnly, long budget)
{
  memset(io, 0, sizeof(*io));
  io->file = fopen(path, readOnly ? "rb" : "r+b");
  check(io->file != NULL);
  check(fseek(io->file, 0, SEEK_END) == 0);
  io->size = (uint64_t)ftell(io->file);
  io->budget = budget;
  io->readOnly = readOnly;
  guest->context = io;
  guest->readAt = testRead;
  guest->writeAt = testWrite;
  guest->flush = testFlush;
  guest->size = io->size;
  guest->readOnly = readOnly;
}

static void ioClose(testIo* io)
{
  fclose(io->file);
  io->file = NULL;
}

static void joinPath(char* target, size_t capacity, const char* directory, const char* name)
{
  check(snprintf(target, capacity, "%s/%s", directory, name) < (int)capacity);
}

static uint8_t* readFileFully(const char* path, size_t* size)
{
  FILE* file = fopen(path, "rb");
  check(file != NULL);
  check(fseek(file, 0, SEEK_END) == 0);
  long length = ftell(file);
  check(length >= 0);
  rewind(file);
  uint8_t* data = malloc((size_t)length + 1);
  check(data != NULL);
  check(fread(data, 1, (size_t)length, file) == (size_t)length);
  fclose(file);
  *size = (size_t)length;
  return data;
}

static void writeFileFully(const char* path, const uint8_t* data, size_t size)
{
  FILE* file = fopen(path, "wb");
  check(file != NULL);
  check(fwrite(data, 1, size, file) == size);
  fclose(file);
}

static void copyFile(const char* source, const char* target)
{
  size_t size;
  uint8_t* data = readFileFully(source, &size);
  writeFileFully(target, data, size);
  free(data);
}

static bool fileExists(const char* path)
{
  struct stat info;
  return stat(path, &info) == 0;
}

static bool filesEqual(const char* first, const char* second)
{
  size_t firstSize;
  size_t secondSize;
  uint8_t* firstData = readFileFully(first, &firstSize);
  uint8_t* secondData = readFileFully(second, &secondSize);
  bool equal = firstSize == secondSize && memcmp(firstData, secondData, firstSize) == 0;
  free(firstData);
  free(secondData);
  return equal;
}

static uint8_t generatedByte(uint32_t index, uint32_t seed)
{
  return (uint8_t)((index * 7u + seed * 13u + (index >> 8)) & 0xFFu);
}

typedef struct
{
  uint32_t size;
  uint32_t seed;
  uint32_t position;
  uint32_t failAt;
} generatorState;

static bool generatorSource(void* context, void* buffer, size_t length)
{
  generatorState* state = context;
  uint8_t* target = buffer;
  if (state->failAt != 0 && state->position + length > state->failAt)
  {
    return false;
  }
  for (size_t index = 0; index < length; ++index)
  {
    target[index] = generatedByte(state->position + (uint32_t)index, state->seed);
  }
  state->position += (uint32_t)length;
  return true;
}

//-- hasAttributes false passes no attributes (the adapter then creates a text file).
static guestFsStatus writeGeneratedAttr(guestFsVolume* volume, uint8_t user, const char* name,
                                        uint32_t size, uint32_t seed, bool overwrite,
                                        bool hasAttributes, uint16_t prodosType, uint16_t auxType,
                                        bool locked)
{
  generatorState state = {size, seed, 0, 0};
  guestFsAttributes attributes;
  memset(&attributes, 0, sizeof(attributes));
  attributes.prodosType = prodosType;
  attributes.auxType = auxType;
  attributes.locked = locked;
  return guestFsWriteEx(volume, user, name, size, generatorSource, &state, overwrite,
                        hasAttributes ? &attributes : NULL);
}

static guestFsStatus writeGenerated(guestFsVolume* volume, const char* name, uint32_t size,
                                    uint32_t seed, bool overwrite)
{
  return writeGeneratedAttr(volume, 0, name, size, seed, overwrite, false, 0, 0, false);
}

static void checkGenerated(const uint8_t* data, size_t length, uint32_t size, uint32_t seed)
{
  check(length == size);
  for (size_t index = 0; index < length; ++index)
  {
    check(data[index] == generatedByte((uint32_t)index, seed));
  }
}

static uint8_t* readGuestFile(guestFsVolume* volume, const char* name, size_t* size)
{
  guestFsEntry entry;
  check(guestFsStat(volume, 0, name, &entry) == guestFsOk);
  check(!entry.damaged && !entry.directory);
  uint8_t* data = malloc(entry.size + 1);
  check(data != NULL);
  size_t got = 0;
  check(guestFsReadAt(volume, 0, name, 0, data, entry.size, &got) == guestFsOk);
  check(got == entry.size);
  *size = got;
  return data;
}

static void checkFileGenerated(guestFsVolume* volume, const char* name, uint32_t size,
                               uint32_t seed)
{
  size_t length;
  uint8_t* data = readGuestFile(volume, name, &length);
  checkGenerated(data, length, size, seed);
  free(data);
}

static bool guestHas(guestFsVolume* volume, const char* name)
{
  guestFsEntry entry;
  return guestFsStat(volume, 0, name, &entry) == guestFsOk;
}

typedef struct
{
  char name[guestFsNameCapacity];
  uint8_t* data;
  size_t size;
  uint16_t prodosType;
  uint16_t auxType;
  bool locked;
  bool damaged;
  char typeLabel[guestFsTypeLabelCapacity];
} snapshotFile;

typedef struct
{
  snapshotFile files[128];
  size_t count;
  guestFsVolume* volume;
} snapshot;

static bool snapshotCollect(void* context, const guestFsEntry* entry)
{
  snapshot* shot = context;
  check(shot->count < 128);
  snapshotFile* file = &shot->files[shot->count++];
  strcpy(file->name, entry->name);
  strcpy(file->typeLabel, entry->typeLabel);
  file->prodosType = entry->attributes.prodosType;
  file->auxType = entry->attributes.auxType;
  file->locked = entry->attributes.locked;
  file->damaged = entry->damaged;
  file->size = entry->size;
  file->data = NULL;
  if (!entry->directory && !entry->damaged)
  {
    file->data = readGuestFile(shot->volume, entry->name, &file->size);
  }
  return true;
}

static void snapshotTake(snapshot* shot, guestFsVolume* volume)
{
  memset(shot, 0, sizeof(*shot));
  shot->volume = volume;
  check(guestFsList(volume, snapshotCollect, shot) == guestFsOk);
}

static void snapshotFree(snapshot* shot)
{
  for (size_t index = 0; index < shot->count; ++index)
  {
    free(shot->files[index].data);
  }
  shot->count = 0;
}

static bool snapshotEqual(const snapshot* first, const snapshot* second)
{
  if (first->count != second->count)
  {
    return false;
  }
  for (size_t index = 0; index < first->count; ++index)
  {
    const snapshotFile* left = &first->files[index];
    bool matched = false;
    for (size_t other = 0; other < second->count && !matched; ++other)
    {
      const snapshotFile* right = &second->files[other];
      matched = strcmp(left->name, right->name) == 0 && left->prodosType == right->prodosType &&
                left->auxType == right->auxType && left->locked == right->locked &&
                strcmp(left->typeLabel, right->typeLabel) == 0 && left->damaged == right->damaged &&
                left->size == right->size && (left->data == NULL) == (right->data == NULL) &&
                (left->data == NULL || memcmp(left->data, right->data, left->size) == 0);
    }
    if (!matched)
    {
      return false;
    }
  }
  return true;
}

typedef struct
{
  char name[32];
  uint32_t size;
  uint32_t seed;
  uint32_t mode;
  uint32_t prodosType;
  uint32_t aux;
  uint32_t locked;
  uint32_t rawType;
  uint32_t address;
  uint32_t readSize;
  uint32_t holes[maxHoles];
  size_t holeCount;
} fixtureFile;

typedef struct
{
  char stem[48];
  char extension[8];
  char imageName[64];
  fixtureFile base[maxFixtureFiles];
  fixtureFile added[maxFixtureFiles];
  size_t baseCount;
  size_t addedCount;
  uint32_t freeSectors;
  uint32_t freeSlots;
  uint32_t freeSectorsAfter;
  uint32_t freeSlotsAfter;
} fixtureInfo;

static void parseHoles(fixtureFile* entry, const char* text)
{
  entry->holeCount = 0;
  if (text[0] == '-')
  {
    return;
  }
  const char* cursor = text;
  while (*cursor != '\0')
  {
    char* end;
    check(entry->holeCount < maxHoles);
    entry->holes[entry->holeCount++] = (uint32_t)strtoul(cursor, &end, 10);
    check(end != cursor);
    cursor = *end == ',' ? end + 1 : end;
  }
}

static void loadFixture(fixtureInfo* fixture, const char* stem)
{
  char path[512];
  char line[200];
  memset(fixture, 0, sizeof(*fixture));
  strcpy(fixture->stem, stem);
  snprintf(path, sizeof(path), "%s/%s.manifest", fixtureDirectory, stem);
  FILE* file = fopen(path, "r");
  check(file != NULL);
  while (fgets(line, sizeof(line), file) != NULL)
  {
    fixtureFile entry;
    char holeText[48];
    unsigned first;
    unsigned second;
    memset(&entry, 0, sizeof(entry));
    if (sscanf(line, "ext %7s", fixture->extension) == 1)
    {
      continue;
    }
    if (sscanf(line, "base %31s %u %u %u %u %u %u %47s", entry.name, &entry.size, &entry.seed,
               &entry.rawType, &entry.address, &entry.locked, &entry.readSize, holeText) == 8)
    {
      check(fixture->baseCount < maxFixtureFiles);
      parseHoles(&entry, holeText);
      fixture->base[fixture->baseCount++] = entry;
    }
    else if (sscanf(line, "add %31s %u %u %u %u %u %u %u %u %u", entry.name, &entry.size,
                    &entry.seed, &entry.mode, &entry.prodosType, &entry.aux, &entry.locked,
                    &entry.rawType, &entry.address, &entry.readSize) == 10)
    {
      check(fixture->addedCount < maxFixtureFiles);
      fixture->added[fixture->addedCount++] = entry;
    }
    else if (sscanf(line, "freeAfter %u %u", &first, &second) == 2)
    {
      fixture->freeSectorsAfter = first;
      fixture->freeSlotsAfter = second;
    }
    else if (sscanf(line, "free %u %u", &first, &second) == 2)
    {
      fixture->freeSectors = first;
      fixture->freeSlots = second;
    }
  }
  fclose(file);
  snprintf(fixture->imageName, sizeof(fixture->imageName), "%s.%s", stem, fixture->extension);
  check(fixture->baseCount > 0 && fixture->addedCount > 0 && fixture->freeSectors != 0);
}

static void fixtureImagePath(char* path, size_t capacity, const fixtureInfo* fixture,
                             const char* which)
{
  char name[96];
  snprintf(name, sizeof(name), "%s.%s.%s", fixture->stem, which, fixture->extension);
  joinPath(path, capacity, fixtureDirectory, name);
}

static void workPath(char* path, size_t capacity, const char* name)
{
  joinPath(path, capacity, outputDirectory, name);
}

static void journalPathFor(char* path, size_t capacity)
{
  workPath(path, capacity, "journal.tmp");
}

static bool journalExists(void)
{
  char journal[512];
  journalPathFor(journal, sizeof(journal));
  return fileExists(journal);
}

static guestFsStatus openImage(const char* imagePath, const char* journalId, testIo* io,
                               guestFsVolume** volume, bool readOnly, long budget, char* detail,
                               size_t detailCapacity)
{
  guestFsIo guest;
  char journal[512];
  ioOpen(io, &guest, imagePath, readOnly, budget);
  journalPathFor(journal, sizeof(journal));
  guestFsStatus status =
      guestFsOpen(&guest, "apple2", journal, journalId, volume, detail, detailCapacity);
  if (status != guestFsOk)
  {
    ioClose(io);
  }
  return status;
}

static guestFsStatus openVolume(const char* imagePath, const fixtureInfo* fixture, testIo* io,
                                guestFsVolume** volume, bool readOnly, long budget, char* detail,
                                size_t detailCapacity)
{
  return openImage(imagePath, fixture->imageName, io, volume, readOnly, budget, detail,
                   detailCapacity);
}

static void closeVolume(guestFsVolume* volume, testIo* io)
{
  guestFsClose(volume);
  ioClose(io);
}

static void checkInfo(guestFsVolume* volume, uint32_t freeSectors, uint32_t freeSlots)
{
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  check(info.freeBlocks == freeSectors);
  check(info.freeDirectoryEntries == freeSlots);
}

static const char* typeLabelOf(uint32_t rawType)
{
  static char unknown[8];
  switch (rawType)
  {
  case 0x00:
    return "T";
  case 0x01:
    return "I";
  case 0x02:
    return "A";
  case 0x04:
    return "B";
  case 0x08:
    return "S";
  case 0x10:
    return "R";
  case 0x20:
    return "A2";
  case 0x40:
    return "B2";
  default:
    snprintf(unknown, sizeof(unknown), "$%02X", (unsigned)(rawType & 0xFFu));
    return unknown;
  }
}

static uint32_t headerBytesOf(uint32_t rawType)
{
  return rawType == 0x01 || rawType == 0x02 ? 2 : (rawType == 0x04 ? 4 : 0);
}

//-- Interchange attributes the adapter reports for a DOS 3.3 file type.
static void attributesOf(uint32_t rawType, uint32_t address, uint32_t* prodosType, uint32_t* aux)
{
  *aux = 0;
  switch (rawType)
  {
  case 0x00:
    *prodosType = 0x04;
    break;
  case 0x01:
    *prodosType = 0xFA;
    *aux = 0x0800;
    break;
  case 0x02:
    *prodosType = 0xFC;
    *aux = 0x0801;
    break;
  case 0x04:
    *prodosType = 0x06;
    *aux = address;
    break;
  case 0x10:
    *prodosType = 0xFE;
    break;
  default:
    *prodosType = 0x06;
    break;
  }
}

//-- Sectors a freshly written file occupies: T/S lists plus data sectors.
static uint32_t sectorsForFile(uint32_t rawType, uint32_t size)
{
  uint32_t data = (size + headerBytesOf(rawType) + 255) / 256;
  return data + (data + 121) / 122;
}

static uint8_t expectedByte(const fixtureFile* file, uint32_t index)
{
  if (index >= file->size)
  {
    return 0;
  }
  uint32_t rawSector = (index + headerBytesOf(file->rawType)) / 256;
  for (size_t hole = 0; hole < file->holeCount; ++hole)
  {
    if (file->holes[hole] == rawSector)
    {
      return 0;
    }
  }
  return generatedByte(index, file->seed);
}

static void checkFileData(guestFsVolume* volume, const fixtureFile* file)
{
  size_t length;
  uint8_t* data = readGuestFile(volume, file->name, &length);
  check(length == file->readSize);
  for (size_t index = 0; index < length; ++index)
  {
    check(data[index] == expectedByte(file, (uint32_t)index));
  }
  free(data);
}

static void checkPieces(guestFsVolume* volume, const fixtureFile* file)
{
  uint8_t piece[300];
  uint32_t stride = file->readSize > 20000 ? 9973 : 97;
  for (uint32_t offset = 0; offset < file->readSize; offset += stride)
  {
    size_t got = 0;
    check(guestFsReadAt(volume, 0, file->name, offset, piece, sizeof(piece), &got) == guestFsOk);
    size_t expectedGot =
        file->readSize - offset < sizeof(piece) ? file->readSize - offset : sizeof(piece);
    check(got == expectedGot);
    for (size_t byte = 0; byte < got; ++byte)
    {
      check(piece[byte] == expectedByte(file, offset + (uint32_t)byte));
    }
  }
  if (file->readSize > 130 * 256)
  {
    uint32_t offset = 122 * 256 - 100;
    size_t got = 0;
    check(guestFsReadAt(volume, 0, file->name, offset, piece, sizeof(piece), &got) == guestFsOk);
    check(got == sizeof(piece));
    for (size_t byte = 0; byte < got; ++byte)
    {
      check(piece[byte] == expectedByte(file, offset + (uint32_t)byte));
    }
  }
  size_t got = 99;
  check(guestFsReadAt(volume, 0, file->name, file->readSize, piece, sizeof(piece), &got) ==
        guestFsOk);
  check(got == 0);
  got = 99;
  check(guestFsReadAt(volume, 0, file->name, file->readSize + 5000, piece, sizeof(piece), &got) ==
        guestFsOk);
  check(got == 0);
}

static void checkEntryAttributes(guestFsVolume* volume, const char* name, uint32_t rawType,
                                 uint32_t address, bool locked)
{
  guestFsEntry entry;
  uint32_t prodosType;
  uint32_t aux;
  attributesOf(rawType, address, &prodosType, &aux);
  check(guestFsStat(volume, 0, name, &entry) == guestFsOk);
  if (entry.attributes.prodosType != prodosType || entry.attributes.auxType != aux)
  {
    fprintf(stderr, "attributes of %s: type %02X aux %04X, expected %02X %04X\n", name,
            (unsigned)entry.attributes.prodosType, (unsigned)entry.attributes.auxType,
            (unsigned)prodosType, (unsigned)aux);
  }
  check(entry.hasAttributes && entry.attributes.prodosType == prodosType &&
        entry.attributes.auxType == aux);
  check(entry.attributes.locked == locked && entry.readOnlyAttribute == locked);
  check(entry.attributes.pascalKind == 5);
  check(strcmp(entry.typeLabel, typeLabelOf(rawType)) == 0);
  check(!entry.directory && !entry.damaged);
}

//-- Dump all files of a volume to <image>.files for the Python cross-check.
typedef struct
{
  guestFsVolume* volume;
  char directory[512];
} dumpContext;

static bool dumpEntry(void* context, const guestFsEntry* entry)
{
  dumpContext* dump = context;
  char name[640];
  if (entry->directory || entry->damaged || strchr(entry->name, '/') != NULL ||
      strchr(entry->name, '?') != NULL)
  {
    return true;
  }
  snprintf(name, sizeof(name), "%s/%s", dump->directory, entry->name);
  FILE* file = fopen(name, "wb");
  check(file != NULL);
  check(guestFsExportToFile(dump->volume, 0, entry->name, file, NULL, NULL) == guestFsOk);
  fclose(file);
  return true;
}

static void dumpVolumeFiles(guestFsVolume* volume, const char* imageName)
{
  dumpContext dump;
  char freePath[640];
  dump.volume = volume;
  snprintf(dump.directory, sizeof(dump.directory), "%s/%s.files", outputDirectory, imageName);
  mkdir(dump.directory, 0755);
  check(guestFsList(volume, dumpEntry, &dump) == guestFsOk);
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  snprintf(freePath, sizeof(freePath), "%s/free.txt", dump.directory);
  FILE* file = fopen(freePath, "w");
  check(file != NULL);
  fprintf(file, "%u %u\n", (unsigned)info.freeBlocks, (unsigned)info.freeDirectoryEntries);
  fclose(file);
}

static void testBaseAndPythonParity(const char* stem)
{
  fixtureInfo fixture;
  char basePath[512];
  char expectedPath[512];
  char imagePath[512];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, stem);
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  fixtureImagePath(expectedPath, sizeof(expectedPath), &fixture, "expected");
  workPath(imagePath, sizeof(imagePath), fixture.imageName);
  copyFile(basePath, imagePath);

  //-- Read-only open: info, list, full and partial reads against the generator.
  check(openVolume(imagePath, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  check(strcmp(info.fsName, "DOS 3.3") == 0 && strcmp(info.nameRule, "dos33") == 0);
  check(strcmp(info.profileName, "140K") == 0);
  check(info.blockSize == 256 && info.totalBlocks == 560 && info.maxUser == 0);
  check(info.directoryEntries == catalogSlots);
  checkInfo(volume, fixture.freeSectors, fixture.freeSlots);
  snapshot shot;
  snapshotTake(&shot, volume);
  check(shot.count == fixture.baseCount);
  snapshotFree(&shot);
  for (size_t index = 0; index < fixture.baseCount; ++index)
  {
    const fixtureFile* file = &fixture.base[index];
    guestFsEntry entry;
    check(guestFsStat(volume, 0, file->name, &entry) == guestFsOk);
    check(entry.size == file->readSize && !entry.damaged && !entry.directory && entry.user == 0);
    check(!entry.systemAttribute);
    if (file->holeCount == 0)
    {
      check(entry.allocatedBytes == sectorsForFile(file->rawType, file->size) * 256);
    }
    checkEntryAttributes(volume, file->name, file->rawType, file->address, file->locked != 0);
    checkFileData(volume, file);
    checkPieces(volume, file);
  }
  guestFsEntry missing;
  check(guestFsStat(volume, 0, "NOSUCHFILE", &missing) == guestFsErrNotFound);
  check(guestFsStat(volume, 0, "hello", &missing) == guestFsOk);
  check(guestFsStat(volume, 0, "GAP1.DAT", &missing) == guestFsErrNotFound);
  check(guestFsStat(volume, 0, "", &missing) == guestFsErrBadName);
  check(guestFsStat(volume, 0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234", &missing) == guestFsErrBadName);
  closeVolume(volume, &io);

  //-- Add the same files the Python reference added: the images must be byte identical.
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  for (size_t index = 0; index < fixture.addedCount; ++index)
  {
    const fixtureFile* file = &fixture.added[index];
    bool explicitAttributes = file->mode != 0;
    check(writeGeneratedAttr(volume, 0, file->name, file->size, file->seed, false,
                             explicitAttributes, (uint16_t)file->prodosType, (uint16_t)file->aux,
                             file->locked != 0) == guestFsOk);
    check(writeGeneratedAttr(volume, 0, file->name, file->size, file->seed, false,
                             explicitAttributes, (uint16_t)file->prodosType, (uint16_t)file->aux,
                             file->locked != 0) == guestFsErrExists);
    checkEntryAttributes(volume, file->name, file->rawType, file->address, file->locked != 0);
  }
  checkInfo(volume, fixture.freeSectorsAfter, fixture.freeSlotsAfter);
  closeVolume(volume, &io);
  check(filesEqual(imagePath, expectedPath));
  check(!journalExists());

  check(openVolume(imagePath, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  for (size_t index = 0; index < fixture.addedCount; ++index)
  {
    size_t length;
    uint8_t* data = readGuestFile(volume, fixture.added[index].name, &length);
    check(length == fixture.added[index].readSize);
    for (size_t position = 0; position < length; ++position)
    {
      check(data[position] == (position < fixture.added[index].size
                                   ? generatedByte((uint32_t)position, fixture.added[index].seed)
                                   : 0));
    }
    free(data);
  }
  dumpVolumeFiles(volume, fixture.imageName);
  closeVolume(volume, &io);
  ++testsRun;
}

static void testEditing(const char* stem)
{
  fixtureInfo fixture;
  char expectedPath[512];
  char imagePath[512];
  char exportPath[512];
  char editName[96];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, stem);
  fixtureImagePath(expectedPath, sizeof(expectedPath), &fixture, "expected");
  snprintf(editName, sizeof(editName), "%s.edit.%s", stem, fixture.extension);
  workPath(imagePath, sizeof(imagePath), editName);
  copyFile(expectedPath, imagePath);
  check(openImage(imagePath, editName, &io, &volume, false, -1, NULL, 0) == guestFsOk);

  //-- Overwrite replaces a file; the old sectors are released together with the new entry.
  guestFsEntry entry;
  guestFsInfo beforeInfo;
  guestFsInfo afterInfo;
  check(writeGenerated(volume, "KEEP.TXT", 2000, 21, false) == guestFsErrExists);
  check(guestFsStat(volume, 0, "KEEP.TXT", &entry) == guestFsOk);
  uint32_t oldSectors = entry.allocatedBytes / 256;
  guestFsInfoGet(volume, &beforeInfo);
  check(writeGenerated(volume, "keep.txt", 2000, 21, true) == guestFsOk);
  guestFsInfoGet(volume, &afterInfo);
  check(afterInfo.freeBlocks + sectorsForFile(0, 2000) == beforeInfo.freeBlocks + oldSectors);
  check(afterInfo.freeDirectoryEntries == beforeInfo.freeDirectoryEntries);
  checkFileGenerated(volume, "KEEP.TXT", 2000, 21);
  checkEntryAttributes(volume, "KEEP.TXT", 0x00, 0, false);
  checkFileGenerated(volume, "HELLO", 1, 1);
  checkFileGenerated(volume, "MID.TXT", 3000, 5);

  //-- Overwriting a sparse file releases exactly the sectors it owned.
  check(guestFsStat(volume, 0, "SPARSE.BIN", &entry) == guestFsOk);
  uint32_t sparseSectors = entry.allocatedBytes / 256;
  guestFsInfoGet(volume, &beforeInfo);
  check(writeGenerated(volume, "SPARSE.BIN", 100, 22, true) == guestFsOk);
  guestFsInfoGet(volume, &afterInfo);
  check(afterInfo.freeBlocks + 2 == beforeInfo.freeBlocks + sparseSectors);
  checkEntryAttributes(volume, "SPARSE.BIN", 0x00, 0, false);
  checkFileGenerated(volume, "SPARSE.BIN", 100, 22);

  //-- Explicit attributes; the lock flag is stored and reported but never enforced.
  check(writeGeneratedAttr(volume, 0, "MID.TXT", 1500, 23, true, true, 0x06, 0x1000, false) ==
        guestFsOk);
  checkEntryAttributes(volume, "MID.TXT", 0x04, 0x1000, false);
  checkFileGenerated(volume, "MID.TXT", 1500, 23);
  check(writeGenerated(volume, "LOCKED.TXT", 300, 24, true) == guestFsOk);
  checkEntryAttributes(volume, "LOCKED.TXT", 0x00, 0, false);
  check(writeGeneratedAttr(volume, 0, "NEWLOCK.DAT", 600, 25, false, true, 0xEF, 3, true) ==
        guestFsOk);
  checkEntryAttributes(volume, "NEWLOCK.DAT", 0x04, 0, true);
  check(guestFsRename(volume, 0, "NEWLOCK.DAT", "RENLOCK.DAT") == guestFsOk);
  checkEntryAttributes(volume, "RENLOCK.DAT", 0x04, 0, true);
  check(guestFsRemove(volume, 0, "RENLOCK.DAT") == guestFsOk);
  check(!guestHas(volume, "RENLOCK.DAT"));
  check(writeGeneratedAttr(volume, 0, "APPLESOFT", 700, 26, false, true, 0xFC, 0x0801, false) ==
        guestFsOk);
  checkEntryAttributes(volume, "APPLESOFT", 0x02, 0, false);
  checkFileGenerated(volume, "APPLESOFT", 700, 26);
  check(writeGeneratedAttr(volume, 0, "INTEGER", 701, 27, false, true, 0xFA, 0x0800, false) ==
        guestFsOk);
  checkEntryAttributes(volume, "INTEGER", 0x01, 0, false);
  checkFileGenerated(volume, "INTEGER", 701, 27);
  check(writeGeneratedAttr(volume, 0, "OBJECT", 1024, 28, false, true, 0xFE, 0, false) ==
        guestFsOk);
  checkEntryAttributes(volume, "OBJECT", 0x10, 0, false);
  checkFileGenerated(volume, "OBJECT", 1024, 28);
  check(guestFsRemove(volume, 0, "APPLESOFT") == guestFsOk);
  check(guestFsRemove(volume, 0, "INTEGER") == guestFsOk);
  check(guestFsRemove(volume, 0, "OBJECT") == guestFsOk);

  //-- Names are stored upper case; creating names are strict, lookups lenient.
  check(writeGenerated(volume, "lower.txt", 77, 29, false) == guestFsOk);
  check(guestFsStat(volume, 0, "LOWER.TXT", &entry) == guestFsOk);
  check(strcmp(entry.name, "LOWER.TXT") == 0);
  check(writeGenerated(volume, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123", 5, 30, false) == guestFsOk);
  checkFileGenerated(volume, "abcdefghijklmnopqrstuvwxyz0123", 5, 30);
  check(writeGenerated(volume, "MY FILE-2 (A)", 5, 31, false) == guestFsOk);
  checkFileGenerated(volume, "My File-2 (a)", 5, 31);
  static const char* const badNames[] = {"",     "ABCDEFGHIJKLMNOPQRSTUVWXYZ01234",
                                         "1ABC", " ABC",
                                         "ABC ", "A,B",
                                         "A\tB", "A~B",
                                         "{ABC", "\xC1\xC2"};
  for (size_t index = 0; index < sizeof(badNames) / sizeof(badNames[0]); ++index)
  {
    check(writeGenerated(volume, badNames[index], 10, 32, false) == guestFsErrBadName);
    check(guestFsRename(volume, 0, "KEEP.TXT", badNames[index]) == guestFsErrBadName);
  }
  check(writeGenerated(volume, "EMPTY.DAT", 0, 33, false) == guestFsErrEmptyFile);
  check(writeGeneratedAttr(volume, 0, "TOOLONG.BIN", 65536, 34, false, true, 0x06, 0, false) ==
        guestFsErrUnsupported);

  //-- Only user 0 exists.
  check(writeGeneratedAttr(volume, 1, "USER1.DAT", 10, 35, false, false, 0, 0, false) ==
        guestFsErrInvalidArgument);
  check(guestFsStat(volume, 1, "KEEP.TXT", &entry) == guestFsErrInvalidArgument);
  size_t got;
  uint8_t piece[16];
  check(guestFsReadAt(volume, 1, "KEEP.TXT", 0, piece, sizeof(piece), &got) ==
        guestFsErrInvalidArgument);
  check(guestFsRemove(volume, 1, "KEEP.TXT") == guestFsErrInvalidArgument);
  check(guestFsRename(volume, 1, "KEEP.TXT", "OTHER") == guestFsErrInvalidArgument);

  //-- A source that fails half way leaves the catalog untouched.
  snapshot before;
  snapshotTake(&before, volume);
  guestFsInfoGet(volume, &beforeInfo);
  generatorState failing = {3000, 36, 0, 600};
  check(guestFsWrite(volume, 0, "FAILED.DAT", 3000, generatorSource, &failing, false) ==
        guestFsErrAborted);
  check(!guestHas(volume, "FAILED.DAT"));
  failing = (generatorState){3000, 36, 0, 600};
  check(guestFsWrite(volume, 0, "KEEP.TXT", 3000, generatorSource, &failing, true) ==
        guestFsErrAborted);
  snapshot after;
  snapshotTake(&after, volume);
  check(snapshotEqual(&before, &after));
  checkInfo(volume, beforeInfo.freeBlocks, beforeInfo.freeDirectoryEntries);
  snapshotFree(&after);
  snapshotFree(&before);

  //-- Rename keeps data and attributes; clashes, missing names and same-name renames.
  check(guestFsRename(volume, 0, "KEEP.TXT", "MID.TXT") == guestFsErrExists);
  check(guestFsRename(volume, 0, "KEEP.TXT", "mid.txt") == guestFsErrExists);
  check(guestFsRename(volume, 0, "NOSUCH", "OTHER") == guestFsErrNotFound);
  check(guestFsRename(volume, 0, "keep.txt", "moved.txt") == guestFsOk);
  check(!guestHas(volume, "KEEP.TXT") && guestHas(volume, "MOVED.TXT"));
  checkFileGenerated(volume, "MOVED.TXT", 2000, 21);
  checkEntryAttributes(volume, "MOVED.TXT", 0x00, 0, false);
  check(guestFsRename(volume, 0, "MOVED.TXT", "MOVED.TXT") == guestFsOk);
  check(guestFsRename(volume, 0, "MOVED.TXT", "Moved.Txt") == guestFsOk);
  checkFileGenerated(volume, "MOVED.TXT", 2000, 21);

  //-- Delete frees the sectors and the slot; the slot is reused by the next new file.
  check(guestFsStat(volume, 0, "MOVED.TXT", &entry) == guestFsOk);
  uint32_t movedSectors = entry.allocatedBytes / 256;
  guestFsInfoGet(volume, &beforeInfo);
  check(guestFsRemove(volume, 0, "MOVED.TXT") == guestFsOk);
  check(guestFsRemove(volume, 0, "MOVED.TXT") == guestFsErrNotFound);
  check(!guestHas(volume, "MOVED.TXT"));
  checkInfo(volume, beforeInfo.freeBlocks + movedSectors, beforeInfo.freeDirectoryEntries + 1);
  check(guestFsReadAt(volume, 0, "MOVED.TXT", 0, piece, sizeof(piece), &got) == guestFsErrNotFound);
  check(writeGenerated(volume, "MOVED.TXT", 1000, 37, false) == guestFsOk);
  checkFileGenerated(volume, "MOVED.TXT", 1000, 37);

  //-- Copy inside the volume keeps file type, load address and lock.
  check(guestFsCopy(volume, 0, "MID.TXT", volume, 0, "COPYOFMID", false, NULL, NULL) == guestFsOk);
  checkEntryAttributes(volume, "COPYOFMID", 0x04, 0x1000, false);
  checkFileGenerated(volume, "COPYOFMID", 1500, 23);
  check(guestFsCopy(volume, 0, "MID.TXT", volume, 0, "COPYOFMID", false, NULL, NULL) ==
        guestFsErrExists);
  check(guestFsCopy(volume, 0, "MID.TXT", volume, 0, "COPYOFMID", true, NULL, NULL) == guestFsOk);
  check(guestFsCopy(volume, 0, "LOCKED2.BIN", volume, 0, "COPYLOCK", false, NULL, NULL) ==
        guestFsOk);
  checkEntryAttributes(volume, "COPYLOCK", 0x04, 0x4000, true);
  checkFileGenerated(volume, "COPYLOCK", 513, 26);
  check(guestFsCopy(volume, 0, "PROG.BAS", volume, 0, "BASCOPY", false, NULL, NULL) == guestFsOk);
  checkEntryAttributes(volume, "BASCOPY", 0x02, 0, false);
  check(guestFsCopy(volume, 0, "TREE.BIN", volume, 0, "TREECOPY", false, NULL, NULL) ==
        guestFsErrNoSpace);
  check(!guestHas(volume, "TREECOPY"));

  //-- Export and import through host files.
  workPath(exportPath, sizeof(exportPath), "export.bin");
  FILE* output = fopen(exportPath, "wb");
  check(output != NULL);
  check(guestFsExportToFile(volume, 0, "BIG.TXT", output, NULL, NULL) == guestFsOk);
  fclose(output);
  size_t exportedSize;
  uint8_t* exported = readFileFully(exportPath, &exportedSize);
  checkGenerated(exported, exportedSize, 1800, 22);
  free(exported);
  FILE* input = fopen(exportPath, "rb");
  check(input != NULL);
  check(guestFsImportFromFile(volume, 0, "IMPORTED.TXT", input, 1800, false, NULL, NULL) ==
        guestFsOk);
  fclose(input);
  checkFileGenerated(volume, "IMPORTED.TXT", 1800, 22);
  checkEntryAttributes(volume, "IMPORTED.TXT", 0x00, 0, false);

  //-- T/S list boundaries: 122 data sectors need one list, 123 need two.
  check(guestFsRemove(volume, 0, "BIG122") == guestFsOk);
  check(guestFsRemove(volume, 0, "TREE.BIN") == guestFsOk);
  check(writeGeneratedAttr(volume, 0, "LIST2.DAT", 31229, 38, false, true, 0x06, 0x7000, false) ==
        guestFsOk);
  checkEntryAttributes(volume, "LIST2.DAT", 0x04, 0x7000, false);
  checkFileGenerated(volume, "LIST2.DAT", 31229, 38);
  check(guestFsStat(volume, 0, "LIST2.DAT", &entry) == guestFsOk);
  check(entry.allocatedBytes == 125 * 256);
  check(writeGeneratedAttr(volume, 0, "LIST2.DAT", 31228, 39, true, true, 0x06, 0x7000, false) ==
        guestFsOk);
  check(guestFsStat(volume, 0, "LIST2.DAT", &entry) == guestFsOk);
  check(entry.allocatedBytes == 123 * 256);
  checkFileGenerated(volume, "LIST2.DAT", 31228, 39);
  check(writeGeneratedAttr(volume, 0, "LIST2.DAT", 62000, 40, true, false, 0, 0, false) ==
        guestFsOk);
  check(guestFsStat(volume, 0, "LIST2.DAT", &entry) == guestFsOk);
  check(entry.allocatedBytes == sectorsForFile(0x00, 62000) * 256);
  checkFileGenerated(volume, "LIST2.DAT", 62000, 40);
  check(guestFsRemove(volume, 0, "LIST2.DAT") == guestFsOk);
  dumpVolumeFiles(volume, editName);
  closeVolume(volume, &io);

  //-- A read-only medium refuses every change but still reads.
  check(openImage(imagePath, editName, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, "RO.DAT", 10, 41, false) == guestFsErrReadOnly);
  check(guestFsRemove(volume, 0, "MID.TXT") == guestFsErrReadOnly);
  check(guestFsRename(volume, 0, "MID.TXT", "RO") == guestFsErrReadOnly);
  checkFileGenerated(volume, "MID.TXT", 1500, 23);
  closeVolume(volume, &io);
  check(!journalExists());
  ++testsRun;
}

static void testFullAndDirectoryFull(void)
{
  fixtureInfo fixture;
  char basePath[512];
  char imagePath[512];
  char secondPath[512];
  char referencePath[512];
  char name[32];
  testIo io;
  guestFsVolume* volume;

  //-- Track 0 is free in this variant: the last sectors handed out are the ones of track 0.
  loadFixture(&fixture, "dos140t0");
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "full-t0.dsk");
  workPath(secondPath, sizeof(secondPath), "full2-t0.dsk");
  workPath(referencePath, sizeof(referencePath), "fullReference.dsk");
  uint32_t freeSectors = fixture.freeSectors;
  check(freeSectors > 100 && freeSectors < 560);
  uint32_t largest = freeSectors;
  while (largest + (largest + 121) / 122 > freeSectors)
  {
    --largest;
  }

  //-- Needing one sector more than is free (lists included): NoSpace, image untouched.
  copyFile(basePath, imagePath);
  copyFile(basePath, referencePath);
  check(openImage(imagePath, "full-t0.dsk", &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(writeGeneratedAttr(volume, 0, "HUGE.DAT", freeSectors * 256, 41, false, true, 0xFE, 0,
                           false) == guestFsErrNoSpace);
  check(writeGeneratedAttr(volume, 0, "HUGE.DAT", (largest + 1) * 256, 41, false, true, 0xFE, 0,
                           false) == guestFsErrNoSpace);
  check(writeGeneratedAttr(volume, 0, "HUGE.DAT", 600 * 256, 41, false, true, 0xFE, 0, false) ==
        guestFsErrNoSpace);
  check(writeGeneratedAttr(volume, 0, "KEEP.TXT", freeSectors * 256, 41, true, true, 0xFE, 0,
                           false) == guestFsErrNoSpace);
  checkFileGenerated(volume, "KEEP.TXT", 100, 12);
  closeVolume(volume, &io);
  check(filesEqual(imagePath, referencePath));
  check(!journalExists());

  //-- The largest file that fits uses every free sector (track 0 included).
  check(openImage(imagePath, "full-t0.dsk", &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(writeGeneratedAttr(volume, 0, "HUGE.DAT", largest * 256, 42, false, true, 0xFE, 0, false) ==
        guestFsOk);
  uint32_t leftOver = freeSectors - largest - (largest + 121) / 122;
  checkInfo(volume, leftOver, fixture.freeSlots - 1);
  checkFileGenerated(volume, "HUGE.DAT", largest * 256, 42);
  if (leftOver == 0)
  {
    check(writeGenerated(volume, "NOROOM.DAT", 1, 43, false) == guestFsErrNoSpace);
  }
  check(writeGenerated(volume, "KEEP.TXT", 20000, 43, true) == guestFsErrNoSpace);
  checkFileGenerated(volume, "KEEP.TXT", 100, 12);
  dumpVolumeFiles(volume, "full-t0.dsk");
  closeVolume(volume, &io);
  copyFile(imagePath, secondPath);
  check(openImage(secondPath, "full2-t0.dsk", &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(guestFsRemove(volume, 0, "HUGE.DAT") == guestFsOk);
  checkInfo(volume, fixture.freeSectors, fixture.freeSlots);
  dumpVolumeFiles(volume, "full2-t0.dsk");
  closeVolume(volume, &io);

  //-- Catalog full: exactly freeSlots two-sector files fit.
  loadFixture(&fixture, "dos140dsk");
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "dirfull.dsk");
  copyFile(basePath, imagePath);
  check(openImage(imagePath, "dirfull.dsk", &io, &volume, false, -1, NULL, 0) == guestFsOk);
  for (uint32_t index = 0; index < fixture.freeSlots; ++index)
  {
    snprintf(name, sizeof(name), "F%03u", (unsigned)index);
    check(writeGenerated(volume, name, 1, index + 1, false) == guestFsOk);
  }
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  check(info.directoryEntries == catalogSlots && info.freeDirectoryEntries == 0);
  check(writeGenerated(volume, "ONEMORE", 1, 44, false) == guestFsErrDirectoryFull);
  checkInfo(volume, fixture.freeSectors - 2 * fixture.freeSlots, 0);
  check(writeGenerated(volume, "KEEP.TXT", 600, 45, true) == guestFsOk);
  checkFileGenerated(volume, "KEEP.TXT", 600, 45);
  check(guestFsRename(volume, 0, "F000", "RENAMED") == guestFsOk);
  check(guestFsRemove(volume, 0, "F001") == guestFsOk);
  check(writeGenerated(volume, "ONEMORE", 1, 44, false) == guestFsOk);
  checkFileGenerated(volume, "ONEMORE", 1, 44);
  dumpVolumeFiles(volume, "dirfull.dsk");
  closeVolume(volume, &io);
  ++testsRun;
}

static void patchImage(const char* sourcePath, const char* targetPath, size_t offset,
                       const uint8_t* bytes, size_t length)
{
  size_t size;
  uint8_t* data = readFileFully(sourcePath, &size);
  check(offset + length <= size);
  memcpy(data + offset, bytes, length);
  writeFileFully(targetPath, data, size);
  free(data);
}

typedef struct
{
  const char* what;
  size_t offset;
  uint8_t bytes[4];
  size_t length;
  guestFsStatus expected;
} corruptCase;

static void testCorruptImages(void)
{
  static const corruptCase cases[] = {
      {"VTOC release 0", sectorAt(17, 0) + 3, {0}, 1, guestFsErrUnsupported},
      {"VTOC release 4", sectorAt(17, 0) + 3, {4}, 1, guestFsErrUnsupported},
      {"pairs per list 121", sectorAt(17, 0) + 0x27, {121}, 1, guestFsErrUnsupported},
      {"36 tracks", sectorAt(17, 0) + 0x34, {36}, 1, guestFsErrUnsupported},
      {"15 sectors", sectorAt(17, 0) + 0x35, {15}, 1, guestFsErrUnsupported},
      {"catalog track 0", sectorAt(17, 0) + 1, {0}, 1, guestFsErrUnsupported},
      {"catalog track 35", sectorAt(17, 0) + 1, {35}, 1, guestFsErrUnsupported},
      {"catalog sector 16", sectorAt(17, 0) + 2, {16}, 1, guestFsErrUnsupported},
      {"catalog link outside", sectorAt(17, 15) + 1, {40, 3}, 2, guestFsErrCorrupt},
      {"catalog link sector 16", sectorAt(17, 14) + 2, {16}, 1, guestFsErrCorrupt},
      {"catalog loop to start", sectorAt(17, 1) + 1, {17, 15}, 2, guestFsErrCorrupt},
      {"catalog loop to itself", sectorAt(17, 8) + 1, {17, 8}, 2, guestFsErrCorrupt},
  };
  fixtureInfo fixture;
  char basePath[512];
  char imagePath[512];
  char detail[guestFsDetailCapacity];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, "dos140dsk");
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "corrupt.dsk");
  for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index)
  {
    patchImage(basePath, imagePath, cases[index].offset, cases[index].bytes, cases[index].length);
    detail[0] = 'x';
    guestFsStatus status =
        openImage(imagePath, fixture.imageName, &io, &volume, true, -1, detail, sizeof(detail));
    if (status != cases[index].expected)
    {
      fprintf(stderr, "corrupt case \"%s\" returned %s\n", cases[index].what,
              guestFsStatusText(status));
    }
    check(status == cases[index].expected);
    check(detail[0] != '\0' && detail[0] != 'x');
  }

  //-- Wrong sizes and foreign content are not DOS 3.3 volumes.
  size_t size;
  uint8_t* data = readFileFully(basePath, &size);
  writeFileFully(imagePath, data, 100000);
  check(openImage(imagePath, fixture.imageName, &io, &volume, true, -1, NULL, 0) ==
        guestFsErrUnsupported);
  memset(data, 0, 143360);
  writeFileFully(imagePath, data, 143360);
  check(openImage(imagePath, "zero.dsk", &io, &volume, true, -1, NULL, 0) == guestFsErrUnsupported);
  free(data);

  //-- Entries whose sector lists are unusable stay listed as damaged: unreadable and not removable,
  //but renamable.
  data = readFileFully(basePath, &size);
  size_t helloList = sectorAt(data[slotAt(15, 0)], data[slotAt(15, 0) + 1]);
  size_t basicList = sectorAt(data[slotAt(15, 4)], data[slotAt(15, 4) + 1]);
  free(data);
  static const struct
  {
    const char* what;
    size_t offset;
    uint8_t bytes[4];
    size_t length;
    const char* name;
  } damagedCases[] = {
      {"list track outside", slotAt(15, 0), {99}, 1, "HELLO"},
      {"list sector outside", slotAt(15, 0) + 1, {16}, 1, "HELLO"},
      {"list loops to itself", 0, {0}, 0, "HELLO"},
      {"data pointer outside", 0, {99}, 1, "HELLO"},
      {"integer file without data", 0, {0}, 0, "BASPROG"},
  };
  for (size_t index = 0; index < sizeof(damagedCases) / sizeof(damagedCases[0]); ++index)
  {
    const char* victimName = damagedCases[index].name;
    switch (index)
    {
    case 2:
    {
      size_t loopSize;
      uint8_t* image = readFileFully(basePath, &loopSize);
      image[helloList + 1] = image[slotAt(15, 0)];
      image[helloList + 2] = image[slotAt(15, 0) + 1];
      writeFileFully(imagePath, image, loopSize);
      free(image);
    }
    break;
    case 3:
      patchImage(basePath, imagePath, helloList + 0x0C, damagedCases[index].bytes, 1);
      break;
    case 4:
    {
      size_t zeroSize;
      uint8_t* image = readFileFully(basePath, &zeroSize);
      memset(image + basicList + 0x0C, 0, 244);
      writeFileFully(imagePath, image, zeroSize);
      free(image);
      break;
    }
    default:
      patchImage(basePath, imagePath, damagedCases[index].offset, damagedCases[index].bytes,
                 damagedCases[index].length);
      break;
    }
    check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
    guestFsEntry entry;
    uint8_t piece[8];
    size_t got;
    snapshot shot;
    snapshotTake(&shot, volume);
    check(shot.count == fixture.baseCount);
    snapshotFree(&shot);
    if (!(guestFsStat(volume, 0, victimName, &entry) == guestFsOk && entry.damaged))
    {
      fprintf(stderr, "damaged case \"%s\" is not reported damaged\n", damagedCases[index].what);
    }
    check(guestFsStat(volume, 0, victimName, &entry) == guestFsOk && entry.damaged);
    check(!entry.hasAttributes);
    check(guestFsReadAt(volume, 0, victimName, 0, piece, sizeof(piece), &got) ==
          guestFsErrDamagedFile);
    check(guestFsRemove(volume, 0, victimName) == guestFsErrDamagedFile);
    check(guestFsCopy(volume, 0, victimName, volume, 0, "COPYDAMAGED", false, NULL, NULL) ==
          guestFsErrDamagedFile);
    check(writeGenerated(volume, victimName, 10, 46, false) == guestFsErrExists);
    check(writeGenerated(volume, victimName, 10, 46, true) == guestFsErrDamagedFile);
    check(guestFsRename(volume, 0, victimName, "RENAMED") == guestFsOk);
    check(guestFsStat(volume, 0, "RENAMED", &entry) == guestFsOk && entry.damaged);
    checkFileGenerated(volume, "MID.TXT", 3000, 5);
    check(writeGenerated(volume, "NEWFILE.DAT", 700, 47, false) == guestFsOk);
    checkFileGenerated(volume, "NEWFILE.DAT", 700, 47);
    closeVolume(volume, &io);
  }

  //-- Type labels, attributes and the lock bit derived from the catalog type byte.
  static const struct
  {
    uint8_t type;
    const char* label;
    uint8_t prodosType;
    bool locked;
  } labelCases[] = {
      {0x00, "T", 0x04, false},   {0x80, "T", 0x04, true},   {0x01, "I", 0xFA, false},
      {0x02, "A", 0xFC, false},   {0x04, "B", 0x06, false},  {0x08, "S", 0x06, false},
      {0x10, "R", 0xFE, false},   {0x20, "A2", 0x06, false}, {0x40, "B2", 0x06, false},
      {0x06, "$06", 0x06, false}, {0x84, "B", 0x06, true},   {0xFF, "$7F", 0x06, true}};
  for (size_t index = 0; index < sizeof(labelCases) / sizeof(labelCases[0]); ++index)
  {
    patchImage(basePath, imagePath, slotAt(15, 0) + 2, &labelCases[index].type, 1);
    check(openImage(imagePath, fixture.imageName, &io, &volume, true, -1, NULL, 0) == guestFsOk);
    guestFsEntry entry;
    check(guestFsStat(volume, 0, "HELLO", &entry) == guestFsOk);
    check(strcmp(entry.typeLabel, labelCases[index].label) == 0);
    check(entry.readOnlyAttribute == labelCases[index].locked);
    if (!entry.damaged)
    {
      check(entry.attributes.prodosType == labelCases[index].prodosType);
      check(entry.attributes.pascalKind == 5);
      check(entry.attributes.locked == labelCases[index].locked);
    }
    closeVolume(volume, &io);
  }
  ++testsRun;
}

typedef enum
{
  mutateCrossLink,
  mutateFreeBit,
  mutateSectorSize,
  mutateCount
} mutation;

//-- A structurally broken image opens (cheap check) but every change fails closed with Corrupt.
static void testMutationValidation(void)
{
  fixtureInfo fixture;
  char basePath[512];
  char imagePath[512];
  char referencePath[512];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, "dos140dsk");
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "mutation.dsk");
  workPath(referencePath, sizeof(referencePath), "mutationReference.dsk");
  for (int kind = 0; kind < mutateCount; ++kind)
  {
    size_t size;
    uint8_t* data = readFileFully(basePath, &size);
    switch ((mutation)kind)
    {
    case mutateCrossLink:
      data[slotAt(15, 1)] = data[slotAt(15, 0)];
      data[slotAt(15, 1) + 1] = data[slotAt(15, 0) + 1];
      break;
    case mutateFreeBit:
    {
      uint8_t track = data[slotAt(15, 0)];
      uint8_t sector = data[slotAt(15, 0) + 1];
      data[sectorAt(17, 0) + 0x38 + 4 * track + (sector < 8 ? 1 : 0)] |=
          (uint8_t)(1u << (sector & 7));
      break;
    }
    case mutateSectorSize:
      data[sectorAt(17, 0) + 0x37] = 2;
      break;
    case mutateCount:
      break;
    }
    writeFileFully(imagePath, data, size);
    writeFileFully(referencePath, data, size);
    free(data);
    check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
    check(writeGenerated(volume, "NEWFILE.DAT", 100, 48, false) == guestFsErrCorrupt);
    check(writeGenerated(volume, "KEEP.TXT", 100, 48, true) == guestFsErrCorrupt);
    check(guestFsRemove(volume, 0, "KEEP.TXT") == guestFsErrCorrupt);
    check(guestFsRename(volume, 0, "KEEP.TXT", "MOVED.TXT") == guestFsErrCorrupt);
    closeVolume(volume, &io);
    check(filesEqual(imagePath, referencePath));
    check(!journalExists());
  }

  //-- Two entries with the same name: reads use the first, changes touching that name fail closed.
  size_t size;
  uint8_t* data = readFileFully(basePath, &size);
  memcpy(data + slotAt(15, 1) + 3, data + slotAt(15, 0) + 3, 30);
  writeFileFully(imagePath, data, size);
  writeFileFully(referencePath, data, size);
  free(data);
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  guestFsEntry entry;
  check(guestFsStat(volume, 0, "HELLO", &entry) == guestFsOk && entry.size == 1);
  check(writeGenerated(volume, "HELLO", 100, 49, true) == guestFsErrCorrupt);
  check(guestFsRemove(volume, 0, "HELLO") == guestFsErrCorrupt);
  check(guestFsRename(volume, 0, "HELLO", "OTHER") == guestFsErrCorrupt);
  check(guestFsRename(volume, 0, "KEEP.TXT", "HELLO") == guestFsErrExists);
  check(guestFsRemove(volume, 0, "KEEP.TXT") == guestFsOk);
  closeVolume(volume, &io);
  ++testsRun;
}

//-- Files move between DOS 3.3 volumes (ProDOS-order 140K into DOS-order 140K) with their
//attributes.
static void testCopyBetweenVolumes(void)
{
  fixtureInfo source;
  fixtureInfo destination;
  char sourcePath[512];
  char destinationPath[512];
  char workDestination[512];
  testIo sourceIo;
  testIo destinationIo;
  guestFsVolume* sourceVolume;
  guestFsVolume* destinationVolume;
  loadFixture(&source, "dos140po");
  loadFixture(&destination, "dos140do");
  fixtureImagePath(sourcePath, sizeof(sourcePath), &source, "expected");
  fixtureImagePath(destinationPath, sizeof(destinationPath), &destination, "base");
  workPath(workDestination, sizeof(workDestination), "copyTarget.do");
  copyFile(destinationPath, workDestination);
  check(openVolume(sourcePath, &source, &sourceIo, &sourceVolume, true, -1, NULL, 0) == guestFsOk);
  check(openImage(workDestination, "copyTarget.do", &destinationIo, &destinationVolume, false, -1,
                  NULL, 0) == guestFsOk);
  check(guestFsCopy(sourceVolume, 0, "PROG.BAS", destinationVolume, 0, "BASCOPY", false, NULL,
                    NULL) == guestFsOk);
  checkEntryAttributes(destinationVolume, "BASCOPY", 0x02, 0, false);
  checkFileGenerated(destinationVolume, "BASCOPY", 10, 23);
  check(guestFsCopy(sourceVolume, 0, "LOCKED2.BIN", destinationVolume, 0, "LOCKCOPY", false, NULL,
                    NULL) == guestFsOk);
  checkEntryAttributes(destinationVolume, "LOCKCOPY", 0x04, 0x4000, true);
  check(guestFsCopy(sourceVolume, 0, "RELOC", destinationVolume, 0, "RELCOPY", false, NULL, NULL) ==
        guestFsOk);
  checkEntryAttributes(destinationVolume, "RELCOPY", 0x10, 0, false);
  checkFileGenerated(destinationVolume, "RELCOPY", 512, 25);
  check(guestFsCopy(sourceVolume, 0, "INTPROG", destinationVolume, 0, "INTCOPY", false, NULL,
                    NULL) == guestFsOk);
  checkEntryAttributes(destinationVolume, "INTCOPY", 0x01, 0, false);
  checkFileGenerated(destinationVolume, "INTCOPY", 300, 24);
  check(guestFsCopy(sourceVolume, 0, "BIG122", destinationVolume, 0, "BIGCOPY", false, NULL,
                    NULL) == guestFsOk);
  checkEntryAttributes(destinationVolume, "BIGCOPY", 0x04, 0x6000, false);
  checkFileGenerated(destinationVolume, "BIGCOPY", 31228, 28);
  check(guestFsCopy(sourceVolume, 0, "SPARSE.BIN", destinationVolume, 0, "SPARCOPY", false, NULL,
                    NULL) == guestFsOk);
  check(guestFsCopy(sourceVolume, 0, "BIG122", destinationVolume, 0, "BIGCOPY2", false, NULL,
                    NULL) == guestFsOk);
  check(guestFsCopy(sourceVolume, 0, "BIG122", destinationVolume, 0, "BIGCOPY3", false, NULL,
                    NULL) == guestFsErrNoSpace);
  check(!guestHas(destinationVolume, "BIGCOPY3"));
  guestFsClose(destinationVolume);
  ioClose(&destinationIo);
  guestFsClose(sourceVolume);
  ioClose(&sourceIo);
  ++testsRun;
}

typedef struct
{
  const char* target;
  long seen;
  long wanted;
} hookState;

static hookState hook;

static bool faultHook(const char* stage)
{
  if (hook.target != NULL && strcmp(stage, hook.target) == 0 && ++hook.seen == hook.wanted)
  {
    return true;
  }
  return false;
}

typedef enum
{
  scenarioOverwrite,
  scenarioDelete,
  scenarioRename,
  scenarioAdd
} scenarioKind;

static guestFsStatus runScenario(guestFsVolume* volume, scenarioKind kind)
{
  switch (kind)
  {
  case scenarioOverwrite:
    return writeGenerated(volume, "KEEP.TXT", 2000, 21, true);
  case scenarioDelete:
    return guestFsRemove(volume, 0, "MID.TXT");
  case scenarioRename:
    return guestFsRename(volume, 0, "KEEP.TXT", "MOVED.TXT");
  case scenarioAdd:
    return writeGenerated(volume, "BRANDNEW.DAT", 1500, 66, false);
  }
  return guestFsErrInvalidArgument;
}

//-- After a simulated crash the visible state must be exactly the old or the new one.
static void crashRecoveryRound(const fixtureInfo* fixture, const char* expectedPath,
                               scenarioKind kind, const char* stage, long stageIndex, long budget,
                               const snapshot* oldShot, const snapshot* newShot, bool* outcomeNew)
{
  char imagePath[512];
  char journal[512];
  testIo io;
  guestFsVolume* volume;
  guestFsIo guest;
  workPath(imagePath, sizeof(imagePath), "crash.dsk");
  journalPathFor(journal, sizeof(journal));
  remove(journal);
  copyFile(expectedPath, imagePath);
  check(openVolume(imagePath, fixture, &io, &volume, false, budget, NULL, 0) == guestFsOk);
  hook.target = stage;
  hook.seen = 0;
  hook.wanted = stageIndex;
  guestFsStatus status = runScenario(volume, kind);
  hook.target = NULL;
  closeVolume(volume, &io);

  //-- "Reboot": journal recovery with a healthy I/O, then re-open.
  ioOpen(&io, &guest, imagePath, false, -1);
  check(guestFsJournalRecover(journal, &guest) == guestFsOk);
  ioClose(&io);
  check(!fileExists(journal));
  check(openVolume(imagePath, fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  snapshot current;
  snapshotTake(&current, volume);
  bool isOld = snapshotEqual(&current, oldShot);
  bool isNew = snapshotEqual(&current, newShot);
  check(isOld || isNew);
  if (status == guestFsOk)
  {
    check(isNew);
  }
  *outcomeNew = isNew && !isOld;
  snapshotFree(&current);
  closeVolume(volume, &io);
  //-- The recovered image must still be a valid, writable DOS 3.3 volume.
  check(openVolume(imagePath, fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, "AFTER.TMP", 300, 77, false) == guestFsOk);
  checkFileGenerated(volume, "AFTER.TMP", 300, 77);
  closeVolume(volume, &io);
}

static void testCrashRecovery(const char* stem, scenarioKind kind)
{
  fixtureInfo fixture;
  char expectedPath[512];
  char imagePath[512];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, stem);
  fixtureImagePath(expectedPath, sizeof(expectedPath), &fixture, "expected");
  guestFsFaultHook = faultHook;

  check(openVolume(expectedPath, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  snapshot oldShot;
  snapshotTake(&oldShot, volume);
  closeVolume(volume, &io);

  //-- Dry run: count I/O operations and build the "new" snapshot.
  workPath(imagePath, sizeof(imagePath), "dry.dsk");
  copyFile(expectedPath, imagePath);
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  hook.target = NULL;
  check(runScenario(volume, kind) == guestFsOk);
  long operations = io.operations;
  snapshot newShot;
  snapshotTake(&newShot, volume);
  closeVolume(volume, &io);
  check(!snapshotEqual(&oldShot, &newShot));

  bool outcomeNew;
  for (long budget = 0; budget <= operations; ++budget)
  {
    crashRecoveryRound(&fixture, expectedPath, kind, NULL, 0, budget, &oldShot, &newShot,
                       &outcomeNew);
    check(budget < operations ? !outcomeNew : outcomeNew);
  }
  static const char* const stages[] = {"dataWritten", "beforeSeal", "sealed",
                                       "slotWritten", "applied",    "committed"};
  for (size_t stageIndex = 0; stageIndex < sizeof(stages) / sizeof(stages[0]); ++stageIndex)
  {
    for (long occurrence = 1; occurrence <= 8; ++occurrence)
    {
      hook.seen = 0;
      crashRecoveryRound(&fixture, expectedPath, kind, stages[stageIndex], occurrence, -1, &oldShot,
                         &newShot, &outcomeNew);
      bool fired = hook.seen >= occurrence;
      check(outcomeNew == (!fired || strcmp(stages[stageIndex], "committed") == 0));
      if (!fired)
      {
        break;
      }
    }
  }
  guestFsFaultHook = NULL;
  snapshotFree(&oldShot);
  snapshotFree(&newShot);
  ++testsRun;
}

typedef struct
{
  char name[32];
  uint32_t type;
  uint32_t aux;
  uint32_t locked;
  uint32_t size;
} realFile;

typedef struct
{
  const uint8_t* data;
  size_t position;
} replayState;

static bool replaySource(void* context, void* buffer, size_t length)
{
  replayState* state = context;
  memcpy(buffer, state->data + state->position, length);
  state->position += length;
  return true;
}

static void testRealImage(unsigned index)
{
  char path[640];
  char line[256];
  char relative[256];
  char extension[8];
  char imagePath[640];
  char workName[48];
  char filesDirectory[640];
  realFile* files = calloc(128, sizeof(realFile));
  size_t fileCount = 0;
  size_t otherCount = 0;
  unsigned freeSectors = 0;
  unsigned freeSlots = 0;
  check(files != NULL);
  snprintf(path, sizeof(path), "%s/real-%u.manifest", fixtureDirectory, index);
  FILE* manifest = fopen(path, "r");
  check(manifest != NULL);
  relative[0] = '\0';
  extension[0] = '\0';
  while (fgets(line, sizeof(line), manifest) != NULL)
  {
    realFile entry;
    unsigned unused;
    memset(&entry, 0, sizeof(entry));
    if (sscanf(line, "image %255[^\n]", relative) == 1 || sscanf(line, "ext %7s", extension) == 1)
    {
      continue;
    }
    if (sscanf(line, "file %31[^|]|%u|%u|%u|%u", entry.name, &entry.type, &entry.aux, &entry.locked,
               &entry.size) == 5)
    {
      check(fileCount < 128);
      files[fileCount++] = entry;
    }
    else if (sscanf(line, "other %u", &unused) == 1)
    {
      ++otherCount;
    }
    else if (sscanf(line, "free %u %u", &freeSectors, &freeSlots) == 2)
    {
      continue;
    }
  }
  fclose(manifest);
  check(relative[0] != '\0' && extension[0] != '\0');
  snprintf(filesDirectory, sizeof(filesDirectory), "%s/real-%u.files", fixtureDirectory, index);
  snprintf(workName, sizeof(workName), "real%u.%s", index, extension);
  joinPath(imagePath, sizeof(imagePath), projectRoot, relative);
  workPath(path, sizeof(path), workName);
  copyFile(imagePath, path);

  //-- Read parity with the Python reference.
  testIo io;
  guestFsVolume* volume;
  char detail[guestFsDetailCapacity];
  detail[0] = '\0';
  guestFsStatus status = openImage(path, workName, &io, &volume, true, -1, detail, sizeof(detail));
  if (status != guestFsOk)
  {
    fprintf(stderr, "real image %s: %s (%s)\n", relative, guestFsStatusText(status), detail);
  }
  check(status == guestFsOk);
  snapshot shot;
  snapshotTake(&shot, volume);
  check(shot.count == fileCount + otherCount);
  snapshotFree(&shot);
  checkInfo(volume, freeSectors, freeSlots);
  for (size_t position = 0; position < fileCount; ++position)
  {
    char dumpPath[900];
    snprintf(dumpPath, sizeof(dumpPath), "%s/%s", filesDirectory, files[position].name);
    size_t expectedSize;
    uint8_t* expected = readFileFully(dumpPath, &expectedSize);
    size_t actualSize;
    uint8_t* actual = readGuestFile(volume, files[position].name, &actualSize);
    check(actualSize == expectedSize && actualSize == files[position].size);
    check(memcmp(actual, expected, actualSize) == 0);
    guestFsEntry entry;
    check(guestFsStat(volume, 0, files[position].name, &entry) == guestFsOk);
    check(entry.attributes.prodosType == files[position].type);
    check(entry.attributes.auxType == files[position].aux);
    check(entry.attributes.locked == (files[position].locked != 0));
    free(actual);
    free(expected);
  }
  closeVolume(volume, &io);

  //-- Round trip: remove a file and put it back, add a new file, rename one.
  check(openImage(path, workName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  size_t victimIndex = fileCount / 2;
  while (victimIndex + 1 < fileCount && files[victimIndex].size == 0)
  {
    ++victimIndex;
  }
  const realFile* victim = fileCount > 0 ? &files[victimIndex] : NULL;
  size_t victimSize = 0;
  uint8_t* victimData = NULL;
  if (victim != NULL)
  {
    victimData = readGuestFile(volume, victim->name, &victimSize);
    status = guestFsRemove(volume, 0, victim->name);
    if (status != guestFsOk)
    {
      fprintf(stderr, "real image %s: remove %s -> %s\n", relative, victim->name,
              guestFsStatusText(status));
    }
    check(status == guestFsOk);
    check(!guestHas(volume, victim->name));
  }
  if (victim != NULL && victimSize > 0)
  {
    guestFsAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.prodosType = (uint16_t)victim->type;
    attributes.auxType = (uint16_t)victim->aux;
    attributes.locked = victim->locked != 0;
    replayState state = {victimData, 0};
    check(guestFsWriteEx(volume, 0, victim->name, (uint32_t)victimSize, replaySource, &state, false,
                         &attributes) == guestFsOk);
    size_t againSize;
    uint8_t* again = readGuestFile(volume, victim->name, &againSize);
    check(againSize == victimSize && memcmp(again, victimData, victimSize) == 0);
    free(again);
    guestFsEntry entry;
    check(guestFsStat(volume, 0, victim->name, &entry) == guestFsOk);
    check(entry.attributes.prodosType == victim->type && entry.attributes.auxType == victim->aux);
    check(entry.attributes.locked == (victim->locked != 0));
  }
  free(victimData);
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  if (info.freeBlocks >= 20 && info.freeDirectoryEntries >= 1)
  {
    check(writeGenerated(volume, "GFSNEW.TXT", 3000, 91, false) == guestFsOk);
    checkFileGenerated(volume, "GFSNEW.TXT", 3000, 91);
  }
  if (fileCount > 1 && (victim != &files[0] || victimSize > 0))
  {
    check(guestFsRename(volume, 0, files[0].name, "GFSRENAM.DAT") == guestFsOk);
    check(guestFsRename(volume, 0, "GFSRENAM.DAT", files[0].name) == guestFsOk);
  }
  dumpVolumeFiles(volume, workName);
  closeVolume(volume, &io);
  free(files);
  ++testsRun;
}

int main(int argc, char** argv)
{
  static const char* const stems[] = {"dos140do", "dos140dsk", "dos140po", "dos140t0"};
  static const char* const crashStems[] = {"dos140dsk", "dos140po"};
  if (argc != 4)
  {
    fprintf(stderr,
            "usage: guestFsDos33Tests <fixtureDirectory> <outputDirectory> <projectRoot>\n");
    return 2;
  }
  fixtureDirectory = argv[1];
  outputDirectory = argv[2];
  projectRoot = argv[3];
  mkdir(outputDirectory, 0755);

  check(guestFsSupportsOs("apple2"));
  for (size_t index = 0; index < sizeof(stems) / sizeof(stems[0]); ++index)
  {
    testBaseAndPythonParity(stems[index]);
    testEditing(stems[index]);
    printf("PASS: guest DOS 3.3 %s\n", stems[index]);
  }
  testFullAndDirectoryFull();
  testCorruptImages();
  testMutationValidation();
  testCopyBetweenVolumes();
  for (size_t index = 0; index < sizeof(crashStems) / sizeof(crashStems[0]); ++index)
  {
    for (int kind = scenarioOverwrite; kind <= scenarioAdd; ++kind)
    {
      testCrashRecovery(crashStems[index], (scenarioKind)kind);
    }
    printf("PASS: crash recovery %s\n", crashStems[index]);
  }
  unsigned realCount = 0;
  for (;; ++realCount)
  {
    char path[640];
    snprintf(path, sizeof(path), "%s/real-%u.manifest", fixtureDirectory, realCount);
    if (!fileExists(path))
    {
      break;
    }
    testRealImage(realCount);
  }
  check(realCount > 0);
  printf("PASS: guestFs DOS 3.3 tests (%d groups, %u real images)\n", testsRun, realCount);
  return 0;
}
