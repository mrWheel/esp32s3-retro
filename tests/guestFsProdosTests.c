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
  headerOffset = 2 * 512 + 4,
  entryBytes = 0x27
};

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

//-- hasAttributes false passes no attributes (the adapter derives type and aux from the name).
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
  bool directory;
  bool damaged;
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
  file->prodosType = entry->attributes.prodosType;
  file->auxType = entry->attributes.auxType;
  file->locked = entry->attributes.locked;
  file->directory = entry->directory;
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
                left->directory == right->directory && left->damaged == right->damaged &&
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
  char name[24];
  uint32_t size;
  uint32_t seed;
  uint32_t mode;
  uint32_t type;
  uint32_t aux;
  uint32_t access;
  uint32_t holes[maxHoles];
  size_t holeCount;
} fixtureFile;

typedef struct
{
  char stem[48];
  char extension[8];
  char imageName[64];
  char subdirectory[24];
  uint32_t profile;
  fixtureFile base[maxFixtureFiles];
  fixtureFile added[maxFixtureFiles];
  size_t baseCount;
  size_t addedCount;
  uint32_t freeBlocks;
  uint32_t freeSlots;
  uint32_t freeBlocksAfter;
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
    if (sscanf(line, "subdir %23s", fixture->subdirectory) == 1)
    {
      continue;
    }
    if (sscanf(line, "profile %u", &first) == 1)
    {
      fixture->profile = first;
    }
    else if (sscanf(line, "base %23s %u %u %u %u %u %47s", entry.name, &entry.size, &entry.seed,
                    &entry.type, &entry.aux, &entry.access, holeText) == 7)
    {
      check(fixture->baseCount < maxFixtureFiles);
      parseHoles(&entry, holeText);
      fixture->base[fixture->baseCount++] = entry;
    }
    else if (sscanf(line, "add %23s %u %u %u %u %u %u", entry.name, &entry.size, &entry.seed,
                    &entry.mode, &entry.type, &entry.aux, &entry.access) == 7)
    {
      check(fixture->addedCount < maxFixtureFiles);
      fixture->added[fixture->addedCount++] = entry;
    }
    else if (sscanf(line, "freeAfter %u %u", &first, &second) == 2)
    {
      fixture->freeBlocksAfter = first;
      fixture->freeSlotsAfter = second;
    }
    else if (sscanf(line, "free %u %u", &first, &second) == 2)
    {
      fixture->freeBlocks = first;
      fixture->freeSlots = second;
    }
  }
  fclose(file);
  snprintf(fixture->imageName, sizeof(fixture->imageName), "%s.%s", stem, fixture->extension);
  check(fixture->baseCount > 0 && fixture->addedCount > 0 && fixture->profile != 0);
  check(fixture->subdirectory[0] != '\0');
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

static void checkInfo(guestFsVolume* volume, uint32_t freeBlocks, uint32_t freeSlots)
{
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  check(info.freeBlocks == freeBlocks);
  check(info.freeDirectoryEntries == freeSlots);
}

static const char* typeLabelOf(uint32_t type)
{
  static char unknown[8];
  static const struct
  {
    uint32_t type;
    const char* label;
  } labels[] = {{0x00, "NON"}, {0x01, "BAD"}, {0x04, "TXT"}, {0x06, "BIN"}, {0x0B, "PCD"},
                {0x0C, "PTX"}, {0x0D, "PDA"}, {0x0F, "DIR"}, {0x19, "ADB"}, {0x1A, "AWP"},
                {0x1B, "ASP"}, {0xB0, "SRC"}, {0xB3, "S16"}, {0xEF, "PAS"}, {0xF0, "CMD"},
                {0xFA, "INT"}, {0xFB, "IVR"}, {0xFC, "BAS"}, {0xFD, "VAR"}, {0xFE, "REL"},
                {0xFF, "SYS"}};
  for (size_t index = 0; index < sizeof(labels) / sizeof(labels[0]); ++index)
  {
    if (labels[index].type == type)
    {
      return labels[index].label;
    }
  }
  snprintf(unknown, sizeof(unknown), "$%02X", (unsigned)type);
  return unknown;
}

static uint8_t pascalKindOfType(uint32_t type)
{
  return type == 0x0B ? 2 : (type == 0x0C ? 3 : 5);
}

static uint32_t blocksForSize(uint32_t size)
{
  uint32_t dataBlocks = (size + 511) / 512;
  if (dataBlocks <= 1)
  {
    return 1;
  }
  if (dataBlocks <= 256)
  {
    return dataBlocks + 1;
  }
  return dataBlocks + (dataBlocks + 255) / 256 + 1;
}

static uint8_t expectedByte(const fixtureFile* file, uint32_t index)
{
  for (size_t hole = 0; hole < file->holeCount; ++hole)
  {
    if (file->holes[hole] == index / 512)
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
  check(length == file->size);
  for (size_t index = 0; index < length; ++index)
  {
    check(data[index] == expectedByte(file, (uint32_t)index));
  }
  free(data);
}

static void checkPieces(guestFsVolume* volume, const fixtureFile* file)
{
  uint8_t piece[300];
  uint32_t stride = file->size > 20000 ? 9973 : 97;
  for (uint32_t offset = 0; offset < file->size; offset += stride)
  {
    size_t got = 0;
    check(guestFsReadAt(volume, 0, file->name, offset, piece, sizeof(piece), &got) == guestFsOk);
    size_t expectedGot = file->size - offset < sizeof(piece) ? file->size - offset : sizeof(piece);
    check(got == expectedGot);
    for (size_t byte = 0; byte < got; ++byte)
    {
      check(piece[byte] == expectedByte(file, offset + (uint32_t)byte));
    }
  }
  if (file->size > 257 * 512)
  {
    uint32_t offset = 255 * 512 + 400;
    size_t got = 0;
    check(guestFsReadAt(volume, 0, file->name, offset, piece, sizeof(piece), &got) == guestFsOk);
    check(got == sizeof(piece));
    for (size_t byte = 0; byte < got; ++byte)
    {
      check(piece[byte] == expectedByte(file, offset + (uint32_t)byte));
    }
  }
  size_t got = 99;
  check(guestFsReadAt(volume, 0, file->name, file->size, piece, sizeof(piece), &got) == guestFsOk);
  check(got == 0);
  got = 99;
  check(guestFsReadAt(volume, 0, file->name, file->size + 5000, piece, sizeof(piece), &got) ==
        guestFsOk);
  check(got == 0);
}

static void checkEntryAttributes(guestFsVolume* volume, const char* name, uint32_t type,
                                 uint32_t aux, bool locked)
{
  guestFsEntry entry;
  check(guestFsStat(volume, 0, name, &entry) == guestFsOk);
  if (entry.attributes.prodosType != type || entry.attributes.auxType != aux)
  {
    fprintf(stderr, "attributes of %s: type %02X aux %04X, expected %02X %04X\n", name,
            (unsigned)entry.attributes.prodosType, (unsigned)entry.attributes.auxType,
            (unsigned)type, (unsigned)aux);
  }
  check(entry.hasAttributes && entry.attributes.prodosType == type &&
        entry.attributes.auxType == aux);
  check(entry.attributes.locked == locked && entry.readOnlyAttribute == locked);
  check(entry.attributes.pascalKind == pascalKindOfType(type));
  check(strcmp(entry.typeLabel, typeLabelOf(type)) == 0);
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
  if (entry->directory || entry->damaged)
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
  check(strcmp(info.fsName, "ProDOS") == 0 && strcmp(info.nameRule, "prodos") == 0);
  check(strcmp(info.profileName, fixture.profile == 280    ? "140K"
                                 : fixture.profile == 1280 ? "640K"
                                                           : "800K") == 0);
  check(info.blockSize == 512 && info.directoryEntries == 51 && info.maxUser == 0);
  check(info.totalBlocks == fixture.profile);
  checkInfo(volume, fixture.freeBlocks, fixture.freeSlots);
  snapshot shot;
  snapshotTake(&shot, volume);
  check(shot.count == fixture.baseCount + 1);
  snapshotFree(&shot);
  for (size_t index = 0; index < fixture.baseCount; ++index)
  {
    const fixtureFile* file = &fixture.base[index];
    guestFsEntry entry;
    check(guestFsStat(volume, 0, file->name, &entry) == guestFsOk);
    check(entry.size == file->size && !entry.damaged && !entry.directory && entry.user == 0);
    check(!entry.systemAttribute);
    if (file->holeCount == 0)
    {
      check(entry.allocatedBytes == blocksForSize(file->size) * 512);
    }
    check(strcmp(entry.typeLabel, typeLabelOf(file->type)) == 0);
    bool locked = (file->access & 0x02) == 0;
    check(entry.hasAttributes && entry.attributes.prodosType == file->type);
    check(entry.attributes.auxType == file->aux && entry.attributes.locked == locked);
    check(entry.readOnlyAttribute == locked);
    check(entry.attributes.pascalKind == pascalKindOfType(file->type));
    checkFileData(volume, file);
    checkPieces(volume, file);
  }
  guestFsEntry missing;
  check(guestFsStat(volume, 0, "NOSUCHFILE", &missing) == guestFsErrNotFound);
  check(guestFsStat(volume, 0, "one.txt", &missing) == guestFsOk);
  check(guestFsStat(volume, 0, "", &missing) == guestFsErrNotFound);
  closeVolume(volume, &io);

  //-- Add the same files the Python reference added: the images must be byte identical.
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  for (size_t index = 0; index < fixture.addedCount; ++index)
  {
    const fixtureFile* file = &fixture.added[index];
    bool locked = (file->access & 0x02) == 0;
    bool explicitAttributes = file->mode != 0;
    check(writeGeneratedAttr(volume, 0, file->name, file->size, file->seed, false,
                             explicitAttributes, (uint16_t)file->type, (uint16_t)file->aux,
                             locked) == guestFsOk);
    check(writeGeneratedAttr(volume, 0, file->name, file->size, file->seed, false,
                             explicitAttributes, (uint16_t)file->type, (uint16_t)file->aux,
                             locked) == guestFsErrExists);
    checkEntryAttributes(volume, file->name, file->type, file->aux, locked);
  }
  checkInfo(volume, fixture.freeBlocksAfter, fixture.freeSlotsAfter);
  closeVolume(volume, &io);
  check(filesEqual(imagePath, expectedPath));
  check(!journalExists());

  check(openVolume(imagePath, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  for (size_t index = 0; index < fixture.addedCount; ++index)
  {
    checkFileGenerated(volume, fixture.added[index].name, fixture.added[index].size,
                       fixture.added[index].seed);
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

  //-- Overwrite replaces a file; the old blocks are released together with the new entry.
  check(writeGenerated(volume, "KEEP.TXT", 2000, 21, false) == guestFsErrExists);
  check(writeGenerated(volume, "keep.txt", 2000, 21, true) == guestFsOk);
  checkFileGenerated(volume, "KEEP.TXT", 2000, 21);
  checkEntryAttributes(volume, "KEEP.TXT", 0x04, 0, false);
  checkFileGenerated(volume, "ONE.TXT", 1, 1);
  checkFileGenerated(volume, "MID.TXT", 3000, 4);

  //-- Overwriting a sparse file releases exactly the blocks it owned.
  guestFsEntry entry;
  guestFsInfo beforeInfo;
  guestFsInfo afterInfo;
  check(guestFsStat(volume, 0, "SPARSE.BIN", &entry) == guestFsOk);
  uint32_t sparseBlocks = entry.allocatedBytes / 512;
  guestFsInfoGet(volume, &beforeInfo);
  check(writeGenerated(volume, "SPARSE.BIN", 100, 22, true) == guestFsOk);
  guestFsInfoGet(volume, &afterInfo);
  check(afterInfo.freeBlocks + 1 == beforeInfo.freeBlocks + sparseBlocks);
  check(afterInfo.freeDirectoryEntries == beforeInfo.freeDirectoryEntries);
  checkEntryAttributes(volume, "SPARSE.BIN", 0x06, 0, false);
  checkFileGenerated(volume, "SPARSE.BIN", 100, 22);

  //-- Explicit attributes; the lock flag is stored and reported but never enforced.
  check(writeGeneratedAttr(volume, 0, "MID.TXT", 1500, 23, true, true, 0x06, 0x1000, false) ==
        guestFsOk);
  checkEntryAttributes(volume, "MID.TXT", 0x06, 0x1000, false);
  checkFileGenerated(volume, "MID.TXT", 1500, 23);
  check(writeGenerated(volume, "LOCKED.TXT", 300, 24, true) == guestFsOk);
  checkEntryAttributes(volume, "LOCKED.TXT", 0x04, 0, false);
  check(writeGeneratedAttr(volume, 0, "NEWLOCK.DAT", 600, 25, false, true, 0xEF, 3, true) ==
        guestFsOk);
  checkEntryAttributes(volume, "NEWLOCK.DAT", 0xEF, 3, true);
  check(guestFsRename(volume, 0, "NEWLOCK.DAT", "RENLOCK.DAT") == guestFsOk);
  checkEntryAttributes(volume, "RENLOCK.DAT", 0xEF, 3, true);
  check(guestFsRemove(volume, 0, "RENLOCK.DAT") == guestFsOk);
  check(!guestHas(volume, "RENLOCK.DAT"));

  //-- Names are stored upper case; creating names are strict, lookups lenient.
  check(writeGenerated(volume, "lower.txt", 77, 26, false) == guestFsOk);
  check(guestFsStat(volume, 0, "LOWER.TXT", &entry) == guestFsOk);
  check(strcmp(entry.name, "LOWER.TXT") == 0);
  check(writeGenerated(volume, "ABCDEFGHIJKLMNO", 5, 27, false) == guestFsOk);
  check(writeGenerated(volume, "A1.B2.C3", 5, 28, false) == guestFsOk);
  static const char* const badNames[] = {
      "",   "ABCDEFGHIJKLMNOP", "1ABC", "A B", "A_B", "A-B", "A/B", "A\tB", ".ABC", "A$B", "A,B",
      "A:B"};
  for (size_t index = 0; index < sizeof(badNames) / sizeof(badNames[0]); ++index)
  {
    check(writeGenerated(volume, badNames[index], 10, 29, false) == guestFsErrBadName);
    check(guestFsRename(volume, 0, "KEEP.TXT", badNames[index]) == guestFsErrBadName);
  }
  check(writeGenerated(volume, "EMPTY.DAT", 0, 30, false) == guestFsErrEmptyFile);
  check(writeGeneratedAttr(volume, 0, "BADTYPE.DAT", 10, 30, false, true, 0x100, 0, false) ==
        guestFsErrInvalidArgument);
  check(writeGeneratedAttr(volume, 0, "DIRTYPE.DAT", 10, 30, false, true, 0x0F, 0, false) ==
        guestFsErrInvalidArgument);

  //-- Only user 0 exists.
  check(writeGeneratedAttr(volume, 1, "USER1.DAT", 10, 31, false, false, 0, 0, false) ==
        guestFsErrInvalidArgument);
  check(guestFsStat(volume, 1, "KEEP.TXT", &entry) == guestFsErrInvalidArgument);
  size_t got;
  uint8_t piece[16];
  check(guestFsReadAt(volume, 1, "KEEP.TXT", 0, piece, sizeof(piece), &got) ==
        guestFsErrInvalidArgument);
  check(guestFsRemove(volume, 1, "KEEP.TXT") == guestFsErrInvalidArgument);
  check(guestFsRename(volume, 1, "KEEP.TXT", "OTHER") == guestFsErrInvalidArgument);

  //-- A source that fails half way leaves the directory untouched.
  snapshot before;
  snapshotTake(&before, volume);
  guestFsInfoGet(volume, &beforeInfo);
  generatorState failing = {3000, 32, 0, 600};
  check(guestFsWrite(volume, 0, "FAILED.DAT", 3000, generatorSource, &failing, false) ==
        guestFsErrAborted);
  check(!guestHas(volume, "FAILED.DAT"));
  failing = (generatorState){3000, 32, 0, 600};
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
  checkEntryAttributes(volume, "MOVED.TXT", 0x04, 0, false);
  check(guestFsRename(volume, 0, "MOVED.TXT", "MOVED.TXT") == guestFsOk);
  check(guestFsRename(volume, 0, "MOVED.TXT", "Moved.Txt") == guestFsOk);
  checkFileGenerated(volume, "MOVED.TXT", 2000, 21);

  //-- Delete frees the blocks and the slot.
  guestFsInfoGet(volume, &beforeInfo);
  check(guestFsRemove(volume, 0, "MOVED.TXT") == guestFsOk);
  check(guestFsRemove(volume, 0, "MOVED.TXT") == guestFsErrNotFound);
  check(!guestHas(volume, "MOVED.TXT"));
  checkInfo(volume, beforeInfo.freeBlocks + 5, beforeInfo.freeDirectoryEntries + 1);
  check(guestFsReadAt(volume, 0, "MOVED.TXT", 0, piece, sizeof(piece), &got) == guestFsErrNotFound);
  check(writeGenerated(volume, "MOVED.TXT", 1000, 33, false) == guestFsOk);
  checkFileGenerated(volume, "MOVED.TXT", 1000, 33);

  //-- Copy inside the volume keeps type, aux and lock even when the new name has no suffix.
  check(guestFsCopy(volume, 0, "MID.TXT", volume, 0, "COPYOFMID", false, NULL, NULL) == guestFsOk);
  checkEntryAttributes(volume, "COPYOFMID", 0x06, 0x1000, false);
  checkFileGenerated(volume, "COPYOFMID", 1500, 23);
  check(guestFsCopy(volume, 0, "MID.TXT", volume, 0, "COPYOFMID", false, NULL, NULL) ==
        guestFsErrExists);
  check(guestFsCopy(volume, 0, "MID.TXT", volume, 0, "COPYOFMID", true, NULL, NULL) == guestFsOk);
  check(guestFsCopy(volume, 0, "LOCKED2.DAT", volume, 0, "COPYLOCK", false, NULL, NULL) ==
        guestFsOk);
  checkEntryAttributes(volume, "COPYLOCK", 0xEF, 0, true);
  check(guestFsCopy(volume, 0, "SAPLING.DAT", volume, 0, "SAPCOPY", false, NULL, NULL) ==
        guestFsOk);
  checkFileGenerated(volume, "SAPCOPY", 40000, 17);

  //-- Export and import through host files.
  workPath(exportPath, sizeof(exportPath), "export.bin");
  FILE* output = fopen(exportPath, "wb");
  check(output != NULL);
  check(guestFsExportToFile(volume, 0, "BIG.TXT", output, NULL, NULL) == guestFsOk);
  fclose(output);
  size_t exportedSize;
  uint8_t* exported = readFileFully(exportPath, &exportedSize);
  checkGenerated(exported, exportedSize, 1800, 12);
  free(exported);
  FILE* input = fopen(exportPath, "rb");
  check(input != NULL);
  check(guestFsImportFromFile(volume, 0, "IMPORTED.TXT", input, 1800, false, NULL, NULL) ==
        guestFsOk);
  fclose(input);
  checkFileGenerated(volume, "IMPORTED.TXT", 1800, 12);
  checkEntryAttributes(volume, "IMPORTED.TXT", 0x04, 0, false);

  //-- A subdirectory is listed but cannot be read, copied, renamed, removed or replaced.
  check(guestFsStat(volume, 0, fixture.subdirectory, &entry) == guestFsOk);
  check(entry.directory && !entry.damaged && strcmp(entry.typeLabel, "DIR") == 0);
  check(guestFsReadAt(volume, 0, fixture.subdirectory, 0, piece, sizeof(piece), &got) ==
        guestFsErrUnsupported);
  check(guestFsRemove(volume, 0, fixture.subdirectory) == guestFsErrUnsupported);
  check(guestFsRename(volume, 0, fixture.subdirectory, "OTHERDIR") == guestFsErrUnsupported);
  check(writeGenerated(volume, fixture.subdirectory, 100, 34, true) == guestFsErrUnsupported);
  check(writeGenerated(volume, fixture.subdirectory, 100, 34, false) == guestFsErrExists);
  check(guestFsCopy(volume, 0, fixture.subdirectory, volume, 0, "DIRCOPY", false, NULL, NULL) !=
        guestFsOk);
  check(!guestHas(volume, "DIRCOPY"));
  check(guestFsRename(volume, 0, "MID.TXT", fixture.subdirectory) == guestFsErrExists);
  dumpVolumeFiles(volume, editName);
  closeVolume(volume, &io);

  //-- A read-only medium refuses every change but still reads.
  check(openImage(imagePath, editName, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, "RO.DAT", 10, 35, false) == guestFsErrReadOnly);
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
  char referencePath[512];
  char name[32];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, "pr140po");
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "full.img");
  workPath(referencePath, sizeof(referencePath), "fullReference.img");
  check(fixture.freeBlocks > 4 && fixture.freeBlocks <= 256);

  //-- A file needing one block more than is free (index block included): NoSpace, image untouched.
  copyFile(basePath, imagePath);
  copyFile(basePath, referencePath);
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  uint32_t tooBig = fixture.freeBlocks * 512;
  check(writeGenerated(volume, "HUGE.DAT", tooBig, 41, false) == guestFsErrNoSpace);
  check(writeGenerated(volume, "HUGE.DAT", 300 * 512, 41, false) == guestFsErrNoSpace);
  check(writeGenerated(volume, "KEEP.TXT", tooBig, 41, true) == guestFsErrNoSpace);
  checkFileGenerated(volume, "KEEP.TXT", 100, 7);
  closeVolume(volume, &io);
  check(filesEqual(imagePath, referencePath));
  check(!journalExists());

  //-- The largest sapling that fits uses every free block; releasing it restores the counts.
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, "HUGE.DAT", (fixture.freeBlocks - 1) * 512, 42, false) == guestFsOk);
  checkInfo(volume, 0, fixture.freeSlots - 1);
  checkFileGenerated(volume, "HUGE.DAT", (fixture.freeBlocks - 1) * 512, 42);
  check(writeGenerated(volume, "NOROOM.DAT", 1, 43, false) == guestFsErrNoSpace);
  check(writeGenerated(volume, "KEEP.TXT", 600, 43, true) == guestFsErrNoSpace);
  checkFileGenerated(volume, "KEEP.TXT", 100, 7);
  check(guestFsRemove(volume, 0, "HUGE.DAT") == guestFsOk);
  checkInfo(volume, fixture.freeBlocks, fixture.freeSlots);
  closeVolume(volume, &io);

  //-- Directory full: exactly freeSlots one-block files fit.
  copyFile(basePath, imagePath);
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  for (uint32_t index = 0; index < fixture.freeSlots; ++index)
  {
    snprintf(name, sizeof(name), "F%03u", (unsigned)index);
    check(writeGenerated(volume, name, 1, index, false) == guestFsOk);
  }
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  check(info.directoryEntries == 51 && info.freeDirectoryEntries == 0);
  check(writeGenerated(volume, "ONEMORE", 1, 44, false) == guestFsErrDirectoryFull);
  checkInfo(volume, fixture.freeBlocks - fixture.freeSlots, 0);
  check(writeGenerated(volume, "KEEP.TXT", 600, 45, true) == guestFsOk);
  checkFileGenerated(volume, "KEEP.TXT", 600, 45);
  check(guestFsRename(volume, 0, "F000", "RENAMED") == guestFsOk);
  check(guestFsRemove(volume, 0, "F001") == guestFsOk);
  check(writeGenerated(volume, "ONEMORE", 1, 44, false) == guestFsOk);
  checkFileGenerated(volume, "ONEMORE", 1, 44);
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

//-- Absolute offset of directory slot n (slot 0 is the volume header) in a ProDOS-order image.
#define slotAt(n) ((2 + (n) / 13) * 512 + 4 + ((n) % 13) * entryBytes)

typedef struct
{
  const char* what;
  size_t offset;
  uint8_t bytes[9];
  size_t length;
  guestFsStatus expected;
} corruptCase;

static void testCorruptImages(void)
{
  static const corruptCase cases[] = {
      {"total beyond the image", headerOffset + 0x25, {0x01, 0x06}, 2, guestFsErrCorrupt},
      {"total below 8", headerOffset + 0x25, {5, 0}, 2, guestFsErrCorrupt},
      {"bitmap pointer 0", headerOffset + 0x23, {0, 0}, 2, guestFsErrCorrupt},
      {"bitmap pointer outside", headerOffset + 0x23, {0x00, 0x10}, 2, guestFsErrCorrupt},
      {"chain pointer below 3", 2 * 512 + 2, {1, 0}, 2, guestFsErrCorrupt},
      {"chain pointer outside", 3 * 512 + 2, {0x00, 0x10}, 2, guestFsErrCorrupt},
      {"chain loop", 5 * 512 + 2, {3, 0}, 2, guestFsErrCorrupt},
      {"name length 0", slotAt(1), {0x10}, 1, guestFsErrCorrupt},
      {"control character in name", slotAt(1) + 1, {7}, 1, guestFsErrCorrupt},
      {"storage E slot", slotAt(1), {0xE7}, 1, guestFsErrCorrupt},
      {"storage F slot", slotAt(1), {0xF7}, 1, guestFsErrCorrupt},
      {"duplicate names",
       slotAt(2),
       {0x17, 'O', 'N', 'E', '.', 'T', 'X', 'T'},
       8,
       guestFsErrCorrupt},
      {"header storage not F", headerOffset, {0xE7}, 1, guestFsErrUnsupported},
      {"entry length not 0x27", headerOffset + 0x1F, {0x28}, 1, guestFsErrUnsupported},
      {"entries per block not 13", headerOffset + 0x20, {12}, 1, guestFsErrUnsupported},
      {"previous pointer set", 2 * 512, {1, 0}, 2, guestFsErrUnsupported},
      {"header name length 0", headerOffset, {0xF0}, 1, guestFsErrUnsupported},
  };
  fixtureInfo fixture;
  char basePath[512];
  char imagePath[512];
  char detail[guestFsDetailCapacity];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, "pr640po");
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "corrupt.img");
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
    if (status == guestFsErrCorrupt)
    {
      check(detail[0] != '\0' && detail[0] != 'x');
    }
  }

  //-- Wrong sizes and foreign content are not ProDOS volumes.
  size_t size;
  uint8_t* data = readFileFully(basePath, &size);
  writeFileFully(imagePath, data, 100000);
  check(openImage(imagePath, fixture.imageName, &io, &volume, true, -1, NULL, 0) ==
        guestFsErrUnsupported);
  memset(data, 0, 143360);
  writeFileFully(imagePath, data, 143360);
  check(openImage(imagePath, "zero.po", &io, &volume, true, -1, NULL, 0) == guestFsErrUnsupported);
  free(data);

  //-- Entries that are not usable files stay listed as damaged: unreadable and not removable, but
  //renamable.
  static const struct
  {
    const char* what;
    size_t offset;
    uint8_t bytes[3];
    size_t length;
  } damagedCases[] = {
      {"storage 5", slotAt(1), {0x57}, 1},
      {"EOF beyond a seedling", slotAt(1) + 0x15, {0x58, 0x02, 0x00}, 3},
      {"key block 0", slotAt(1) + 0x11, {0x00, 0x00}, 2},
  };
  for (size_t index = 0; index < sizeof(damagedCases) / sizeof(damagedCases[0]); ++index)
  {
    patchImage(basePath, imagePath, damagedCases[index].offset, damagedCases[index].bytes,
               damagedCases[index].length);
    check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
    guestFsEntry entry;
    uint8_t piece[8];
    size_t got;
    snapshot shot;
    snapshotTake(&shot, volume);
    check(shot.count == fixture.baseCount + 1);
    snapshotFree(&shot);
    check(guestFsStat(volume, 0, "ONE.TXT", &entry) == guestFsOk && entry.damaged);
    check(!entry.directory);
    check(guestFsReadAt(volume, 0, "ONE.TXT", 0, piece, sizeof(piece), &got) ==
          guestFsErrDamagedFile);
    check(guestFsRemove(volume, 0, "ONE.TXT") == guestFsErrDamagedFile);
    check(guestFsCopy(volume, 0, "ONE.TXT", volume, 0, "COPYONE", false, NULL, NULL) ==
          guestFsErrDamagedFile);
    check(guestFsRename(volume, 0, "ONE.TXT", "TWO.TXT") == guestFsOk);
    check(guestFsStat(volume, 0, "TWO.TXT", &entry) == guestFsOk && entry.damaged);
    check(writeGenerated(volume, "TWO.TXT", 10, 46, false) == guestFsErrExists);
    check(writeGenerated(volume, "TWO.TXT", 10, 46, true) == guestFsErrDamagedFile);
    checkFileGenerated(volume, "A512.BIN", 512, 2);
    check(writeGenerated(volume, "NEWFILE.DAT", 700, 47, false) == guestFsOk);
    checkFileGenerated(volume, "NEWFILE.DAT", 700, 47);
    closeVolume(volume, &io);
  }

  //-- Type labels and the Pascal kind derived from the file type.
  static const struct
  {
    uint8_t type;
    const char* label;
    uint8_t kind;
  } labelCases[] = {{0xAA, "$AA", 5}, {0x0B, "PCD", 2}, {0x0C, "PTX", 3}, {0x0D, "PDA", 5},
                    {0x06, "BIN", 5}, {0x00, "NON", 5}, {0xFF, "SYS", 5}, {0xFC, "BAS", 5}};
  for (size_t index = 0; index < sizeof(labelCases) / sizeof(labelCases[0]); ++index)
  {
    patchImage(basePath, imagePath, slotAt(1) + 0x10, &labelCases[index].type, 1);
    check(openImage(imagePath, fixture.imageName, &io, &volume, true, -1, NULL, 0) == guestFsOk);
    guestFsEntry entry;
    check(guestFsStat(volume, 0, "ONE.TXT", &entry) == guestFsOk && !entry.damaged);
    check(strcmp(entry.typeLabel, labelCases[index].label) == 0);
    check(entry.attributes.prodosType == labelCases[index].type);
    check(entry.attributes.pascalKind == labelCases[index].kind);
    closeVolume(volume, &io);
  }
  ++testsRun;
}

typedef enum
{
  mutateCrossLink,
  mutateFreeBit,
  mutateIndexPointer,
  mutateOutsideKey,
  mutateCount
} mutation;

static uint16_t le16At(const uint8_t* data, size_t offset)
{
  return (uint16_t)(data[offset] | (data[offset + 1] << 8));
}

//-- A structurally broken image opens (cheap check) but every change fails closed with Corrupt.
static void testMutationValidation(void)
{
  fixtureInfo fixture;
  char basePath[512];
  char imagePath[512];
  char referencePath[512];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, "pr640po");
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "mutation.img");
  workPath(referencePath, sizeof(referencePath), "mutationReference.img");
  for (int kind = 0; kind < mutateCount; ++kind)
  {
    size_t size;
    uint8_t* data = readFileFully(basePath, &size);
    uint16_t bitmapBlock = le16At(data, headerOffset + 0x23);
    uint16_t keyOne = le16At(data, slotAt(1) + 0x11);
    uint16_t keySparse = le16At(data, slotAt(9) + 0x11);
    const char* readName = NULL;
    switch ((mutation)kind)
    {
    case mutateCrossLink:
      data[slotAt(2) + 0x11] = data[slotAt(1) + 0x11];
      data[slotAt(2) + 0x12] = data[slotAt(1) + 0x12];
      break;
    case mutateFreeBit:
      data[(size_t)bitmapBlock * 512 + keyOne / 8] |= (uint8_t)(0x80 >> (keyOne & 7));
      break;
    case mutateIndexPointer:
      data[(size_t)keySparse * 512] = 0xFF;
      data[(size_t)keySparse * 512 + 256] = 0xFF;
      readName = "SPARSE.BIN";
      break;
    case mutateOutsideKey:
      data[slotAt(1) + 0x11] = 0xFF;
      data[slotAt(1) + 0x12] = 0x7F;
      readName = "ONE.TXT";
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
    if (readName != NULL)
    {
      uint8_t piece[16];
      size_t got;
      check(guestFsReadAt(volume, 0, readName, 0, piece, sizeof(piece), &got) ==
            guestFsErrDamagedFile);
    }
    closeVolume(volume, &io);
    check(filesEqual(imagePath, referencePath));
    check(!journalExists());
  }
  ++testsRun;
}

//-- A file moves between ProDOS volumes (640K ProDOS order into 140K DOS order) with its
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
  loadFixture(&source, "pr640po");
  loadFixture(&destination, "pr140do");
  fixtureImagePath(sourcePath, sizeof(sourcePath), &source, "expected");
  fixtureImagePath(destinationPath, sizeof(destinationPath), &destination, "base");
  workPath(workDestination, sizeof(workDestination), "copyTarget.do");
  copyFile(destinationPath, workDestination);
  check(openVolume(sourcePath, &source, &sourceIo, &sourceVolume, true, -1, NULL, 0) == guestFsOk);
  check(openImage(workDestination, "copyTarget.do", &destinationIo, &destinationVolume, false, -1,
                  NULL, 0) == guestFsOk);
  check(guestFsCopy(sourceVolume, 0, "PROG.BAS", destinationVolume, 0, "BASCOPY", false, NULL,
                    NULL) == guestFsOk);
  checkEntryAttributes(destinationVolume, "BASCOPY", 0xFC, 0x0801, false);
  checkFileGenerated(destinationVolume, "BASCOPY", 10, 13);
  check(guestFsCopy(sourceVolume, 0, "LOCKED2.DAT", destinationVolume, 0, "LOCKCOPY", false, NULL,
                    NULL) == guestFsOk);
  checkEntryAttributes(destinationVolume, "LOCKCOPY", 0xEF, 0, true);
  check(guestFsCopy(sourceVolume, 0, "SAPLING.DAT", destinationVolume, 0, "SAPCOPY", false, NULL,
                    NULL) == guestFsOk);
  checkEntryAttributes(destinationVolume, "SAPCOPY", 0xFD, 7, false);
  checkFileGenerated(destinationVolume, "SAPCOPY", 40000, 17);
  check(guestFsCopy(sourceVolume, 0, "SPARSET.BIN", destinationVolume, 0, "SPARCOPY", false, NULL,
                    NULL) == guestFsErrNoSpace);
  check(!guestHas(destinationVolume, "SPARCOPY"));
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
    return writeGenerated(volume, "KEEP.TXT", 2000, 55, true);
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
  workPath(imagePath, sizeof(imagePath), "crash.img");
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
  //-- The recovered image must still be a valid, writable ProDOS volume.
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
  workPath(imagePath, sizeof(imagePath), "dry.img");
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
  uint32_t access;
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
  unsigned freeBlocks = 0;
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
    unsigned storage;
    char otherName[32];
    memset(&entry, 0, sizeof(entry));
    if (sscanf(line, "image %255s", relative) == 1 || sscanf(line, "ext %7s", extension) == 1)
    {
      continue;
    }
    if (sscanf(line, "file %31s %u %u %u %u", entry.name, &entry.type, &entry.aux, &entry.access,
               &entry.size) == 5)
    {
      check(fileCount < 128);
      files[fileCount++] = entry;
    }
    else if (sscanf(line, "other %31s %u", otherName, &storage) == 2)
    {
      ++otherCount;
    }
    else if (sscanf(line, "free %u %u", &freeBlocks, &freeSlots) == 2)
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
  checkInfo(volume, freeBlocks, freeSlots);
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
    checkEntryAttributes(volume, files[position].name, files[position].type, files[position].aux,
                         (files[position].access & 0x02) == 0);
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
    check(guestFsRemove(volume, 0, victim->name) == guestFsOk);
    check(!guestHas(volume, victim->name));
  }
  if (victim != NULL && victimSize > 0)
  {
    guestFsAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.prodosType = (uint16_t)victim->type;
    attributes.auxType = (uint16_t)victim->aux;
    attributes.locked = (victim->access & 0x02) == 0;
    replayState state = {victimData, 0};
    check(guestFsWriteEx(volume, 0, victim->name, (uint32_t)victimSize, replaySource, &state, false,
                         &attributes) == guestFsOk);
    size_t againSize;
    uint8_t* again = readGuestFile(volume, victim->name, &againSize);
    check(againSize == victimSize && memcmp(again, victimData, victimSize) == 0);
    free(again);
    checkEntryAttributes(volume, victim->name, victim->type, victim->aux,
                         (victim->access & 0x02) == 0);
  }
  free(victimData);
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  if (info.freeBlocks >= 10 && info.freeDirectoryEntries >= 1)
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
  static const char* const stems[] = {"pr140po", "pr640po", "pr800po", "pr140do", "pr140dsk"};
  static const char* const crashStems[] = {"pr140po", "pr140do", "pr800po"};
  if (argc != 4)
  {
    fprintf(stderr,
            "usage: guestFsProdosTests <fixtureDirectory> <outputDirectory> <projectRoot>\n");
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
    printf("PASS: guest ProDOS %s\n", stems[index]);
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
  printf("PASS: guestFs ProDOS tests (%d groups, %u real images)\n", testsRun, realCount);
  return 0;
}
