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
  maxFixtureFiles = 16,
  directoryOffset = 2 * 512,
  slotZeroOffset = directoryOffset + 26,
  slotOneOffset = directoryOffset + 52
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

//-- kind 0 passes no attributes (the adapter derives the kind from the file name).
static guestFsStatus writeGeneratedKind(guestFsVolume* volume, uint8_t user, const char* name,
                                        uint32_t size, uint32_t seed, bool overwrite, uint8_t kind)
{
  generatorState state = {size, seed, 0, 0};
  guestFsAttributes attributes;
  memset(&attributes, 0, sizeof(attributes));
  attributes.pascalKind = kind;
  return guestFsWriteEx(volume, user, name, size, generatorSource, &state, overwrite,
                        kind != 0 ? &attributes : NULL);
}

static guestFsStatus writeGenerated(guestFsVolume* volume, const char* name, uint32_t size,
                                    uint32_t seed, bool overwrite)
{
  return writeGeneratedKind(volume, 0, name, size, seed, overwrite, 0);
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
  check(!entry.damaged);
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

typedef struct
{
  char name[guestFsNameCapacity];
  uint8_t* data;
  size_t size;
  uint8_t kind;
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
  file->kind = entry->attributes.pascalKind;
  file->data = readGuestFile(shot->volume, entry->name, &file->size);
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
      matched = strcmp(left->name, right->name) == 0 && left->kind == right->kind &&
                left->size == right->size && memcmp(left->data, right->data, left->size) == 0;
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
  uint32_t kind;
} fixtureFile;

typedef struct
{
  char stem[48];
  char extension[8];
  char imageName[64];
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

static void loadFixture(fixtureInfo* fixture, const char* stem)
{
  char path[512];
  char line[160];
  memset(fixture, 0, sizeof(*fixture));
  strcpy(fixture->stem, stem);
  snprintf(path, sizeof(path), "%s/%s.manifest", fixtureDirectory, stem);
  FILE* file = fopen(path, "r");
  check(file != NULL);
  while (fgets(line, sizeof(line), file) != NULL)
  {
    fixtureFile entry;
    unsigned first;
    unsigned second;
    if (sscanf(line, "ext %7s", fixture->extension) == 1)
    {
      continue;
    }
    if (sscanf(line, "profile %u", &first) == 1)
    {
      fixture->profile = first;
    }
    else if (sscanf(line, "base %23s %u %u %u", entry.name, &entry.size, &entry.seed,
                    &entry.kind) == 4)
    {
      check(fixture->baseCount < maxFixtureFiles);
      fixture->base[fixture->baseCount++] = entry;
    }
    else if (sscanf(line, "add %23s %u %u %u", entry.name, &entry.size, &entry.seed, &entry.kind) ==
             4)
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

static guestFsStatus openImage(const char* imagePath, const char* journalId, testIo* io,
                               guestFsVolume** volume, bool readOnly, long budget, char* detail,
                               size_t detailCapacity)
{
  guestFsIo guest;
  char journal[512];
  ioOpen(io, &guest, imagePath, readOnly, budget);
  journalPathFor(journal, sizeof(journal));
  guestFsStatus status =
      guestFsOpen(&guest, "ucsd", journal, journalId, volume, detail, detailCapacity);
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

static const char* kindLabel(uint32_t kind)
{
  static const char* const labels[] = {"?",    "BAD",  "CODE", "TEXT",  "INFO",
                                       "DATA", "GRAF", "FOTO", "SECDIR"};
  return kind < 9 ? labels[kind] : "?";
}

static uint16_t prodosTypeOfKind(uint32_t kind)
{
  return kind == 2 ? 0x0B : (kind == 3 ? 0x0C : (kind == 5 ? 0x0D : 0x06));
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
  check(strcmp(info.fsName, "UCSD Pascal") == 0 && strcmp(info.nameRule, "pascal") == 0);
  check(strcmp(info.profileName, fixture.profile == 280    ? "140K"
                                 : fixture.profile == 1280 ? "640K"
                                                           : "800K") == 0);
  check(info.blockSize == 512 && info.directoryEntries == 77 && info.maxUser == 0);
  check(info.totalBlocks == fixture.profile - 6);
  checkInfo(volume, fixture.freeBlocks, fixture.freeSlots);
  snapshot shot;
  snapshotTake(&shot, volume);
  check(shot.count == fixture.baseCount);
  snapshotFree(&shot);
  for (size_t index = 0; index < fixture.baseCount; ++index)
  {
    const fixtureFile* file = &fixture.base[index];
    guestFsEntry entry;
    check(guestFsStat(volume, 0, file->name, &entry) == guestFsOk);
    check(entry.size == file->size && !entry.damaged && !entry.directory && entry.user == 0);
    check(entry.allocatedBytes == (file->size + 511) / 512 * 512);
    check(strcmp(entry.typeLabel, kindLabel(file->kind)) == 0);
    check(entry.hasAttributes && entry.attributes.pascalKind == file->kind);
    check(entry.attributes.prodosType == prodosTypeOfKind(file->kind));
    checkFileGenerated(volume, file->name, file->size, file->seed);
    uint8_t piece[300];
    for (uint32_t offset = 0; offset < file->size; offset += 97)
    {
      size_t got = 0;
      check(guestFsReadAt(volume, 0, file->name, offset, piece, sizeof(piece), &got) == guestFsOk);
      size_t expectedGot =
          file->size - offset < sizeof(piece) ? file->size - offset : sizeof(piece);
      check(got == expectedGot);
      for (size_t byte = 0; byte < got; ++byte)
      {
        check(piece[byte] == generatedByte(offset + (uint32_t)byte, file->seed));
      }
    }
    size_t got = 99;
    check(guestFsReadAt(volume, 0, file->name, file->size, piece, sizeof(piece), &got) ==
          guestFsOk);
    check(got == 0);
    got = 99;
    check(guestFsReadAt(volume, 0, file->name, file->size + 5000, piece, sizeof(piece), &got) ==
          guestFsOk);
    check(got == 0);
  }
  guestFsEntry missing;
  check(guestFsStat(volume, 0, "NOSUCHFILE", &missing) == guestFsErrNotFound);
  check(guestFsStat(volume, 0, "one.text", &missing) == guestFsOk);
  check(guestFsStat(volume, 0, "", &missing) == guestFsErrNotFound);
  closeVolume(volume, &io);

  //-- Add the same files the Python reference added: the images must be byte identical.
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  for (size_t index = 0; index < fixture.addedCount; ++index)
  {
    const fixtureFile* file = &fixture.added[index];
    check(writeGeneratedKind(volume, 0, file->name, file->size, file->seed, false,
                             (uint8_t)file->kind) == guestFsOk);
    check(writeGeneratedKind(volume, 0, file->name, file->size, file->seed, false,
                             (uint8_t)file->kind) == guestFsErrExists);
  }
  checkInfo(volume, fixture.freeBlocksAfter, fixture.freeSlotsAfter);
  closeVolume(volume, &io);
  check(filesEqual(imagePath, expectedPath));
  check(!fileExists("journal.tmp"));

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

static bool guestHas(guestFsVolume* volume, const char* name)
{
  guestFsEntry entry;
  return guestFsStat(volume, 0, name, &entry) == guestFsOk;
}

static void testEditing(const char* stem)
{
  fixtureInfo fixture;
  char expectedPath[512];
  char imagePath[512];
  char exportPath[512];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, stem);
  fixtureImagePath(expectedPath, sizeof(expectedPath), &fixture, "expected");
  workPath(imagePath, sizeof(imagePath), "edit.img");
  copyFile(expectedPath, imagePath);
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);

  //-- Overwrite grows a file; the old extent is released only by the directory commit.
  check(writeGenerated(volume, "KEEP.TEXT", 2000, 21, false) == guestFsErrExists);
  check(writeGenerated(volume, "keep.text", 2000, 21, true) == guestFsOk);
  checkFileGenerated(volume, "KEEP.TEXT", 2000, 21);
  guestFsEntry entry;
  check(guestFsStat(volume, 0, "KEEP.TEXT", &entry) == guestFsOk &&
        entry.attributes.pascalKind == 3);
  checkFileGenerated(volume, "MID.INFO", 3000, 4);

  //-- Names are stored upper case; creating names are strict, lookups lenient.
  check(writeGenerated(volume, "lower.text", 77, 22, false) == guestFsOk);
  check(guestFsStat(volume, 0, "LOWER.TEXT", &entry) == guestFsOk);
  check(strcmp(entry.name, "LOWER.TEXT") == 0);
  check(writeGenerated(volume, "ABCDEFGHIJKLMNO", 5, 23, false) == guestFsOk);
  static const char* const badNames[] = {
      "",    "ABCDEFGHIJKLMNOP", "A B", "A$B", "A=B", "A?B", "A,B", "A[B", "A#B", "A:B", "A/B",
      "A\tB"};
  for (size_t index = 0; index < sizeof(badNames) / sizeof(badNames[0]); ++index)
  {
    check(writeGenerated(volume, badNames[index], 10, 24, false) == guestFsErrBadName);
    check(guestFsRename(volume, 0, "KEEP.TEXT", badNames[index]) == guestFsErrBadName);
  }
  check(writeGenerated(volume, "EMPTY.DATA", 0, 25, false) == guestFsErrEmptyFile);

  //-- Only user 0 exists.
  check(writeGeneratedKind(volume, 1, "USER1.DATA", 10, 26, false, 0) == guestFsErrInvalidArgument);
  check(guestFsStat(volume, 1, "KEEP.TEXT", &entry) == guestFsErrInvalidArgument);
  size_t got;
  uint8_t piece[16];
  check(guestFsReadAt(volume, 1, "KEEP.TEXT", 0, piece, sizeof(piece), &got) ==
        guestFsErrInvalidArgument);
  check(guestFsRemove(volume, 1, "KEEP.TEXT") == guestFsErrInvalidArgument);
  check(guestFsRename(volume, 1, "KEEP.TEXT", "OTHER") == guestFsErrInvalidArgument);

  //-- A source that fails half way leaves the directory untouched.
  snapshot before;
  snapshotTake(&before, volume);
  guestFsInfo beforeInfo;
  guestFsInfoGet(volume, &beforeInfo);
  generatorState failing = {3000, 27, 0, 600};
  check(guestFsWrite(volume, 0, "FAILED.DATA", 3000, generatorSource, &failing, false) ==
        guestFsErrAborted);
  check(!guestHas(volume, "FAILED.DATA"));
  snapshot after;
  snapshotTake(&after, volume);
  check(snapshotEqual(&before, &after));
  checkInfo(volume, beforeInfo.freeBlocks, beforeInfo.freeDirectoryEntries);
  snapshotFree(&after);
  snapshotFree(&before);

  //-- Rename keeps data and kind; clashes, missing names and same-name renames.
  check(guestFsRename(volume, 0, "KEEP.TEXT", "MID.INFO") == guestFsErrExists);
  check(guestFsRename(volume, 0, "NOSUCH", "OTHER") == guestFsErrNotFound);
  check(guestFsRename(volume, 0, "KEEP.TEXT", "moved.text") == guestFsOk);
  check(!guestHas(volume, "KEEP.TEXT") && guestHas(volume, "MOVED.TEXT"));
  checkFileGenerated(volume, "MOVED.TEXT", 2000, 21);
  check(guestFsRename(volume, 0, "MOVED.TEXT", "MOVED.TEXT") == guestFsOk);
  checkFileGenerated(volume, "MOVED.TEXT", 2000, 21);

  //-- Delete frees the blocks and the slot.
  guestFsInfoGet(volume, &beforeInfo);
  check(guestFsRemove(volume, 0, "MOVED.TEXT") == guestFsOk);
  check(guestFsRemove(volume, 0, "MOVED.TEXT") == guestFsErrNotFound);
  check(!guestHas(volume, "MOVED.TEXT"));
  checkInfo(volume, beforeInfo.freeBlocks + 4, beforeInfo.freeDirectoryEntries + 1);
  check(guestFsReadAt(volume, 0, "MOVED.TEXT", 0, piece, sizeof(piece), &got) ==
        guestFsErrNotFound);
  check(writeGenerated(volume, "MOVED.TEXT", 1000, 28, false) == guestFsOk);
  checkFileGenerated(volume, "MOVED.TEXT", 1000, 28);

  //-- Copy inside the volume keeps the kind even when the new name has no suffix.
  check(guestFsCopy(volume, 0, "MID.INFO", volume, 0, "COPYOFMID", false, NULL, NULL) == guestFsOk);
  check(guestFsStat(volume, 0, "COPYOFMID", &entry) == guestFsOk &&
        entry.attributes.pascalKind == 4);
  checkFileGenerated(volume, "COPYOFMID", 3000, 4);
  check(guestFsCopy(volume, 0, "MID.INFO", volume, 0, "COPYOFMID", false, NULL, NULL) ==
        guestFsErrExists);
  check(guestFsCopy(volume, 0, "MID.INFO", volume, 0, "COPYOFMID", true, NULL, NULL) == guestFsOk);

  //-- Export and import through host files.
  workPath(exportPath, sizeof(exportPath), "export.bin");
  FILE* output = fopen(exportPath, "wb");
  check(output != NULL);
  check(guestFsExportToFile(volume, 0, "BIG.TEXT", output, NULL, NULL) == guestFsOk);
  fclose(output);
  size_t exportedSize;
  uint8_t* exported = readFileFully(exportPath, &exportedSize);
  checkGenerated(exported, exportedSize, 1800, 12);
  free(exported);
  FILE* input = fopen(exportPath, "rb");
  check(input != NULL);
  check(guestFsImportFromFile(volume, 0, "IMPORTED.TEXT", input, 1800, false, NULL, NULL) ==
        guestFsOk);
  fclose(input);
  checkFileGenerated(volume, "IMPORTED.TEXT", 1800, 12);
  check(guestFsStat(volume, 0, "IMPORTED.TEXT", &entry) == guestFsOk &&
        entry.attributes.pascalKind == 3);
  closeVolume(volume, &io);

  //-- A read-only medium refuses every change but still reads.
  check(openImage(imagePath, fixture.imageName, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, "RO.DATA", 10, 29, false) == guestFsErrReadOnly);
  check(guestFsRemove(volume, 0, "MID.INFO") == guestFsErrReadOnly);
  check(guestFsRename(volume, 0, "MID.INFO", "RO") == guestFsErrReadOnly);
  checkFileGenerated(volume, "MID.INFO", 3000, 4);
  closeVolume(volume, &io);
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
  loadFixture(&fixture, "p140po");
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "full.img");
  workPath(referencePath, sizeof(referencePath), "fullReference.img");

  //-- Enough free blocks in total but no contiguous run: NoSpace, image untouched.
  copyFile(basePath, imagePath);
  copyFile(basePath, referencePath);
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  uint32_t tail = fixture.freeBlocks - 5;
  check(writeGenerated(volume, "HUGE.DATA", (tail + 1) * 512, 31, false) == guestFsErrNoSpace);
  check(writeGenerated(volume, "HUGE.DATA", 300 * 512, 31, false) == guestFsErrNoSpace);
  check(writeGenerated(volume, "KEEP.TEXT", (tail + 8) * 512, 31, true) == guestFsErrNoSpace);
  checkFileGenerated(volume, "KEEP.TEXT", 100, 7);
  closeVolume(volume, &io);
  check(filesEqual(imagePath, referencePath));
  check(!fileExists("journal.tmp"));

  //-- The largest run still fits; afterwards small files go into the holes.
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, "HUGE.DATA", tail * 512 + 1 - 512, 32, false) == guestFsOk);
  checkInfo(volume, fixture.freeBlocks - tail, fixture.freeSlots - 1);
  check(writeGenerated(volume, "HOLE2.DATA", 1000, 33, false) == guestFsOk);
  check(writeGenerated(volume, "HOLE3.DATA", 1500, 34, false) == guestFsOk);
  check(writeGenerated(volume, "NOROOM.DATA", 1, 35, false) == guestFsErrNoSpace);
  checkInfo(volume, 0, fixture.freeSlots - 3);
  closeVolume(volume, &io);

  //-- Directory full: exactly freeSlots one-block files fit.
  copyFile(basePath, imagePath);
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  for (uint32_t index = 0; index < fixture.freeSlots; ++index)
  {
    snprintf(name, sizeof(name), "F%03u", (unsigned)index);
    check(writeGenerated(volume, name, 1, index, false) == guestFsOk);
  }
  check(writeGenerated(volume, "ONEMORE", 1, 36, false) == guestFsErrDirectoryFull);
  checkInfo(volume, fixture.freeBlocks - fixture.freeSlots, 0);
  check(writeGenerated(volume, "KEEP.TEXT", 600, 37, true) == guestFsOk);
  checkFileGenerated(volume, "KEEP.TEXT", 600, 37);
  check(guestFsRename(volume, 0, "F000", "RENAMED") == guestFsOk);
  check(guestFsRemove(volume, 0, "F001") == guestFsOk);
  check(writeGenerated(volume, "ONEMORE", 1, 36, false) == guestFsOk);
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
  uint8_t bytes[9];
  size_t length;
  guestFsStatus expected;
} corruptCase;

static void testCorruptImages(void)
{
  static const corruptCase cases[] = {
      {"directory end 2", directoryOffset + 2, {2, 0}, 2, guestFsErrCorrupt},
      {"directory end 33", directoryOffset + 2, {33, 0}, 2, guestFsErrCorrupt},
      {"directory end 0", directoryOffset + 2, {0, 0}, 2, guestFsErrCorrupt},
      {"total beyond the image", directoryOffset + 14, {0x01, 0x05}, 2, guestFsErrCorrupt},
      {"file count beyond capacity", directoryOffset + 16, {78, 0}, 2, guestFsErrCorrupt},
      {"first block in directory", slotZeroOffset, {3, 0}, 2, guestFsErrCorrupt},
      {"next block beyond total", slotZeroOffset + 2, {0x01, 0x05}, 2, guestFsErrCorrupt},
      {"next not after first", slotZeroOffset + 2, {6, 0}, 2, guestFsErrCorrupt},
      {"last bytes 0", slotZeroOffset + 22, {0, 0}, 2, guestFsErrCorrupt},
      {"last bytes 513", slotZeroOffset + 22, {1, 2}, 2, guestFsErrCorrupt},
      {"name length 0", slotZeroOffset + 6, {0}, 1, guestFsErrCorrupt},
      {"name length 16", slotZeroOffset + 6, {16}, 1, guestFsErrCorrupt},
      {"control character in name", slotZeroOffset + 7, {7}, 1, guestFsErrCorrupt},
      {"overlapping files", slotOneOffset, {6, 0}, 2, guestFsErrCorrupt},
      {"duplicate names",
       slotOneOffset + 6,
       {8, 'O', 'N', 'E', '.', 'T', 'E', 'X', 'T'},
       9,
       guestFsErrCorrupt},
      {"first word not zero", directoryOffset, {1, 0}, 2, guestFsErrUnsupported},
      {"volume name length 0", directoryOffset + 6, {0}, 1, guestFsErrUnsupported},
      {"volume name length 8", directoryOffset + 6, {8}, 1, guestFsErrUnsupported},
      {"volume block count 5", directoryOffset + 14, {5, 0}, 2, guestFsErrUnsupported},
  };
  fixtureInfo fixture;
  char basePath[512];
  char imagePath[512];
  char detail[guestFsDetailCapacity];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, "p640po");
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

  //-- Wrong sizes and foreign content are not Pascal volumes.
  size_t size;
  uint8_t* data = readFileFully(basePath, &size);
  writeFileFully(imagePath, data, 100000);
  check(openImage(imagePath, fixture.imageName, &io, &volume, true, -1, NULL, 0) ==
        guestFsErrUnsupported);
  memset(data, 0, 143360);
  writeFileFully(imagePath, data, 143360);
  check(openImage(imagePath, "zero.po", &io, &volume, true, -1, NULL, 0) == guestFsErrUnsupported);
  free(data);

  //-- A kind outside 1..8 makes only that entry damaged: listed, unreadable, but
  //renamable/removable.
  static const uint8_t damagedKinds[] = {0x00, 0x09};
  for (size_t index = 0; index < sizeof(damagedKinds); ++index)
  {
    uint8_t kindBytes[2] = {damagedKinds[index], 0};
    patchImage(basePath, imagePath, slotZeroOffset + 4, kindBytes, 2);
    check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
    guestFsEntry entry;
    check(guestFsStat(volume, 0, "ONE.TEXT", &entry) == guestFsOk && entry.damaged);
    uint8_t piece[8];
    size_t got;
    check(guestFsReadAt(volume, 0, "ONE.TEXT", 0, piece, sizeof(piece), &got) ==
          guestFsErrDamagedFile);
    check(guestFsRename(volume, 0, "ONE.TEXT", "TWO.TEXT") == guestFsOk);
    check(guestFsRemove(volume, 0, "TWO.TEXT") == guestFsOk);
    closeVolume(volume, &io);
  }

  //-- Unknown upper bits of the kind word survive a rename.
  uint8_t keepBits[2] = {0x13, 0x00};
  patchImage(basePath, imagePath, slotZeroOffset + 4, keepBits, 2);
  check(openImage(imagePath, fixture.imageName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(guestFsRename(volume, 0, "ONE.TEXT", "RENAMED.TEXT") == guestFsOk);
  closeVolume(volume, &io);
  data = readFileFully(imagePath, &size);
  check(data[slotZeroOffset + 4] == 0x13);
  free(data);
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
    return writeGenerated(volume, "KEEP.TEXT", 2000, 55, true);
  case scenarioDelete:
    return guestFsRemove(volume, 0, "MID.INFO");
  case scenarioRename:
    return guestFsRename(volume, 0, "KEEP.TEXT", "MOVED.TEXT");
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
  //-- The recovered image must still be a valid, writable Pascal volume.
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

static void testJournalGarbage(void)
{
  char journal[512];
  char imagePath[512];
  char basePath[512];
  fixtureInfo fixture;
  testIo io;
  guestFsIo guest;
  loadFixture(&fixture, "p140po");
  journalPathFor(journal, sizeof(journal));
  fixtureImagePath(basePath, sizeof(basePath), &fixture, "base");
  workPath(imagePath, sizeof(imagePath), "garbage.img");
  copyFile(basePath, imagePath);
  ioOpen(&io, &guest, imagePath, false, -1);
  remove(journal);
  check(guestFsJournalRecover(journal, &guest) == guestFsOk);
  FILE* file = fopen(journal, "wb");
  check(file != NULL);
  fputs("short", file);
  fclose(file);
  char identity[64];
  check(guestFsJournalPeek(journal, identity, sizeof(identity)) == guestFsErrCorrupt);
  check(guestFsJournalRecover(journal, NULL) == guestFsOk);
  check(!fileExists(journal));

  //-- A sealed journal whose payload is damaged is kept and reported.
  guestFsJournal made;
  check(guestFsJournalBegin(&made, journal, fixture.imageName, io.size) == guestFsOk);
  check(guestFsJournalSave(&made, &guest, directoryOffset, 32) == guestFsOk);
  check(guestFsJournalSeal(&made) == guestFsOk);
  guestFsJournalAbandon(&made);
  check(guestFsJournalPeek(journal, identity, sizeof(identity)) == guestFsOk &&
        strcmp(identity, fixture.imageName) == 0);
  FILE* damage = fopen(journal, "r+b");
  check(damage != NULL);
  check(fseek(damage, 148 + 20, SEEK_SET) == 0);
  fputc(0x55, damage);
  fclose(damage);
  check(guestFsJournalRecover(journal, &guest) == guestFsErrCorrupt);
  check(fileExists(journal));
  remove(journal);

  //-- A journal for a differently sized image is not applied.
  check(guestFsJournalBegin(&made, journal, fixture.imageName, io.size + 512) == guestFsOk);
  check(guestFsJournalSave(&made, &guest, directoryOffset, 32) == guestFsOk);
  check(guestFsJournalSeal(&made) == guestFsOk);
  guestFsJournalAbandon(&made);
  check(guestFsJournalRecover(journal, &guest) == guestFsErrCorrupt);
  check(fileExists(journal));
  remove(journal);
  ioClose(&io);
  remove(imagePath);
  ++testsRun;
}

//-- A file moves between a 640K and a 140K Pascal volume with its kind.
static void testCopyBetweenVolumes(void)
{
  fixtureInfo source;
  fixtureInfo destination;
  char sourcePath[512];
  char destinationPath[512];
  testIo sourceIo;
  testIo destinationIo;
  guestFsVolume* sourceVolume;
  guestFsVolume* destinationVolume;
  loadFixture(&source, "p640po");
  loadFixture(&destination, "p140do");
  fixtureImagePath(sourcePath, sizeof(sourcePath), &source, "expected");
  fixtureImagePath(destinationPath, sizeof(destinationPath), &destination, "base");
  char workDestination[512];
  workPath(workDestination, sizeof(workDestination), "copyTarget.do");
  copyFile(destinationPath, workDestination);
  check(openVolume(sourcePath, &source, &sourceIo, &sourceVolume, true, -1, NULL, 0) == guestFsOk);
  check(openImage(workDestination, "copyTarget.do", &destinationIo, &destinationVolume, false, -1,
                  NULL, 0) == guestFsOk);
  check(guestFsCopy(sourceVolume, 0, "SMALL.GRAF", destinationVolume, 0, "GRAFCOPY", false, NULL,
                    NULL) == guestFsOk);
  guestFsEntry entry;
  check(guestFsStat(destinationVolume, 0, "GRAFCOPY", &entry) == guestFsOk &&
        entry.attributes.pascalKind == 6);
  checkFileGenerated(destinationVolume, "GRAFCOPY", 10, 13);
  guestFsClose(destinationVolume);
  ioClose(&destinationIo);
  guestFsClose(sourceVolume);
  ioClose(&sourceIo);
  ++testsRun;
}

typedef struct
{
  char name[24];
  uint32_t kind;
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
  char imagePath[640];
  char workName[32];
  char filesDirectory[640];
  realFile* files = calloc(128, sizeof(realFile));
  size_t fileCount = 0;
  unsigned freeBlocks = 0;
  unsigned freeSlots = 0;
  check(files != NULL);
  snprintf(path, sizeof(path), "%s/real-%u.manifest", fixtureDirectory, index);
  FILE* manifest = fopen(path, "r");
  check(manifest != NULL);
  relative[0] = '\0';
  while (fgets(line, sizeof(line), manifest) != NULL)
  {
    realFile entry;
    if (sscanf(line, "image %255s", relative) == 1)
    {
      continue;
    }
    if (sscanf(line, "file %23s %u %u", entry.name, &entry.kind, &entry.size) == 3)
    {
      check(fileCount < 128);
      files[fileCount++] = entry;
    }
    else if (sscanf(line, "free %u %u", &freeBlocks, &freeSlots) == 2)
    {
      continue;
    }
  }
  fclose(manifest);
  check(relative[0] != '\0');
  snprintf(filesDirectory, sizeof(filesDirectory), "%s/real-%u.files", fixtureDirectory, index);
  snprintf(workName, sizeof(workName), "real%u.po", index);
  joinPath(imagePath, sizeof(imagePath), projectRoot, relative);
  workPath(path, sizeof(path), workName);
  copyFile(imagePath, path);

  //-- Read parity with the Python reference.
  testIo io;
  guestFsVolume* volume;
  char detail[guestFsDetailCapacity];
  guestFsStatus status = openImage(path, workName, &io, &volume, true, -1, detail, sizeof(detail));
  if (status != guestFsOk)
  {
    fprintf(stderr, "real image %s: %s (%s)\n", relative, guestFsStatusText(status), detail);
  }
  check(status == guestFsOk);
  snapshot shot;
  snapshotTake(&shot, volume);
  check(shot.count == fileCount);
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
    guestFsEntry entry;
    check(guestFsStat(volume, 0, files[position].name, &entry) == guestFsOk);
    check(entry.attributes.pascalKind == files[position].kind);
    free(actual);
    free(expected);
  }
  closeVolume(volume, &io);

  //-- Round trip: remove a file and put it back, add a new file, rename one.
  if (fileCount > 0)
  {
    check(openImage(path, workName, &io, &volume, false, -1, NULL, 0) == guestFsOk);
    const realFile* victim = &files[fileCount / 2];
    size_t victimSize;
    uint8_t* victimData = readGuestFile(volume, victim->name, &victimSize);
    check(guestFsRemove(volume, 0, victim->name) == guestFsOk);
    check(!guestHas(volume, victim->name));
    guestFsAttributes attributes;
    memset(&attributes, 0, sizeof(attributes));
    attributes.pascalKind = (uint8_t)victim->kind;
    replayState state = {victimData, 0};
    check(guestFsWriteEx(volume, 0, victim->name, (uint32_t)victimSize, replaySource, &state, false,
                         &attributes) == guestFsOk);
    size_t againSize;
    uint8_t* again = readGuestFile(volume, victim->name, &againSize);
    check(againSize == victimSize && memcmp(again, victimData, victimSize) == 0);
    free(again);
    free(victimData);
    check(writeGenerated(volume, "GFSNEW.TEXT", 3000, 91, false) == guestFsOk);
    if (fileCount > 1)
    {
      check(guestFsRename(volume, 0, files[0].name, "GFSRENAM.DAT") == guestFsOk);
      check(guestFsRename(volume, 0, "GFSRENAM.DAT", files[0].name) == guestFsOk);
    }
    dumpVolumeFiles(volume, workName);
    closeVolume(volume, &io);
  }
  free(files);
  ++testsRun;
}

int main(int argc, char** argv)
{
  static const char* const stems[] = {"p140po", "p640po", "p800po", "p140do", "p140dsk"};
  static const char* const crashStems[] = {"p140po", "p140do", "p800po"};
  if (argc != 4)
  {
    fprintf(stderr,
            "usage: guestFsPascalTests <fixtureDirectory> <outputDirectory> <projectRoot>\n");
    return 2;
  }
  fixtureDirectory = argv[1];
  outputDirectory = argv[2];
  projectRoot = argv[3];
  mkdir(outputDirectory, 0755);

  check(guestFsSupportsOs("ucsd") && guestFsSupportsOs("apple2") && !guestFsSupportsOs("swtpc"));
  for (size_t index = 0; index < sizeof(stems) / sizeof(stems[0]); ++index)
  {
    testBaseAndPythonParity(stems[index]);
    testEditing(stems[index]);
    printf("PASS: guest Pascal %s\n", stems[index]);
  }
  testFullAndDirectoryFull();
  testCorruptImages();
  testCopyBetweenVolumes();
  testJournalGarbage();
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
  printf("PASS: guestFs Pascal tests (%d groups, %u real images)\n", testsRun, realCount);
  return 0;
}
