#include "guestFs.h"
#include "guestFsInternal.h"
#include "guestFsImage.h"
#include "imageFile.h"
#include <assert.h>
#include <dirent.h>
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

static void copyFile(const char* source, const char* target)
{
  size_t size;
  uint8_t* data = readFileFully(source, &size);
  FILE* file = fopen(target, "wb");
  check(file != NULL);
  check(fwrite(data, 1, size, file) == size);
  fclose(file);
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

static guestFsStatus writeGenerated(guestFsVolume* volume, uint8_t user, const char* name,
                                    uint32_t size, uint32_t seed, bool overwrite)
{
  generatorState state = {size, seed, 0, 0};
  return guestFsWrite(volume, user, name, size, generatorSource, &state, overwrite);
}

static void checkGenerated(const uint8_t* data, size_t length, uint32_t size, uint32_t seed)
{
  check(length == ((size_t)size + 127) / 128 * 128);
  for (size_t index = 0; index < length; ++index)
  {
    check(data[index] == (index < size ? generatedByte((uint32_t)index, seed) : 0x1A));
  }
}

static uint8_t* readGuestFile(guestFsVolume* volume, uint8_t user, const char* name, size_t* size)
{
  guestFsEntry entry;
  check(guestFsStat(volume, user, name, &entry) == guestFsOk);
  check(!entry.damaged);
  uint8_t* data = malloc(entry.size + 1);
  check(data != NULL);
  size_t got = 0;
  check(guestFsReadAt(volume, user, name, 0, data, entry.size, &got) == guestFsOk);
  check(got == entry.size);
  *size = got;
  return data;
}

typedef struct
{
  char name[guestFsNameCapacity];
  uint8_t user;
  uint8_t* data;
  size_t size;
} snapshotFile;

typedef struct
{
  snapshotFile files[512];
  size_t count;
  guestFsVolume* volume;
} snapshot;

static bool snapshotCollect(void* context, const guestFsEntry* entry)
{
  snapshot* shot = context;
  check(shot->count < 512);
  snapshotFile* file = &shot->files[shot->count++];
  strcpy(file->name, entry->name);
  file->user = entry->user;
  file->data = readGuestFile(shot->volume, entry->user, entry->name, &file->size);
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
      matched = left->user == right->user && strcmp(left->name, right->name) == 0 &&
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
  char os[16];
  char profile[16];
  char stem[48];
  uint32_t directoryOffset;
  uint32_t blockSize;
  uint32_t directoryEntries;
  uint32_t maxBlock;
  uint32_t firstBlock;
  struct
  {
    char name[24];
    uint32_t size;
    uint32_t seed;
  } base[8], added[8];
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
    char name[24];
    unsigned size;
    unsigned seed;
    unsigned first;
    unsigned second;
    unsigned third;
    unsigned fourth;
    unsigned fifth;
    if (sscanf(line, "os %15s", fixture->os) == 1 ||
        sscanf(line, "profile %15s", fixture->profile) == 1)
    {
      continue;
    }
    if (sscanf(line, "geometry %u %u %u %u %u", &first, &second, &third, &fourth, &fifth) == 5)
    {
      fixture->directoryOffset = first;
      fixture->blockSize = second;
      fixture->directoryEntries = third;
      fixture->maxBlock = fourth;
      fixture->firstBlock = fifth;
    }
    else if (sscanf(line, "base %23s %u %u", name, &size, &seed) == 3)
    {
      strcpy(fixture->base[fixture->baseCount].name, name);
      fixture->base[fixture->baseCount].size = size;
      fixture->base[fixture->baseCount++].seed = seed;
    }
    else if (sscanf(line, "add %23s %u %u", name, &size, &seed) == 3)
    {
      strcpy(fixture->added[fixture->addedCount].name, name);
      fixture->added[fixture->addedCount].size = size;
      fixture->added[fixture->addedCount++].seed = seed;
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
  check(fixture->baseCount > 0 && fixture->addedCount > 0 && fixture->blockSize != 0);
}

static void workPath(char* path, size_t capacity, const char* name)
{
  joinPath(path, capacity, outputDirectory, name);
}

static void journalPathFor(char* path, size_t capacity)
{
  workPath(path, capacity, "journal.tmp");
}

static guestFsStatus openVolume(const char* imagePath, const fixtureInfo* fixture, testIo* io,
                                guestFsVolume** volume, bool readOnly, long budget, char* detail,
                                size_t detailCapacity)
{
  guestFsIo guest;
  char journal[512];
  ioOpen(io, &guest, imagePath, readOnly, budget);
  journalPathFor(journal, sizeof(journal));
  guestFsStatus status =
      guestFsOpen(&guest, fixture->os, journal, fixture->stem, volume, detail, detailCapacity);
  if (status != guestFsOk)
  {
    ioClose(io);
  }
  return status;
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

//-- Dump all files of a volume to <image>.files for the Python cross-check.
static bool dumpEntry(void* context, const guestFsEntry* entry);

typedef struct
{
  guestFsVolume* volume;
  char directory[512];
} dumpContext;

static bool dumpEntry(void* context, const guestFsEntry* entry)
{
  dumpContext* dump = context;
  char name[600];
  snprintf(name, sizeof(name), "%s/%u-%s", dump->directory, entry->user, entry->name);
  FILE* file = fopen(name, "wb");
  check(file != NULL);
  check(guestFsExportToFile(dump->volume, entry->user, entry->name, file, NULL, NULL) == guestFsOk);
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
  char name[96];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, stem);
  snprintf(name, sizeof(name), "%s.base.dsk", stem);
  joinPath(basePath, sizeof(basePath), fixtureDirectory, name);
  snprintf(name, sizeof(name), "%s.expected.dsk", stem);
  joinPath(expectedPath, sizeof(expectedPath), fixtureDirectory, name);
  snprintf(name, sizeof(name), "%s.dsk", stem);
  workPath(imagePath, sizeof(imagePath), name);
  copyFile(basePath, imagePath);

  //-- Read-only open: list, info, full and partial reads against the generator.
  check(openVolume(imagePath, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  check(strcmp(info.fsName, "CP/M") == 0 && strcmp(info.profileName, fixture.profile) == 0);
  check(info.blockSize == fixture.blockSize && info.directoryEntries == fixture.directoryEntries);
  checkInfo(volume, fixture.freeBlocks, fixture.freeSlots);
  snapshot shot;
  snapshotTake(&shot, volume);
  check(shot.count == fixture.baseCount);
  for (size_t index = 0; index < fixture.baseCount; ++index)
  {
    size_t size;
    uint8_t* data = readGuestFile(volume, 0, fixture.base[index].name, &size);
    checkGenerated(data, size, fixture.base[index].size, fixture.base[index].seed);
    free(data);
    uint8_t piece[300];
    for (uint32_t offset = 0; offset < size; offset += 97)
    {
      size_t got = 0;
      check(guestFsReadAt(volume, 0, fixture.base[index].name, offset, piece, sizeof(piece),
                          &got) == guestFsOk);
      size_t expectedGot = size - offset < sizeof(piece) ? size - offset : sizeof(piece);
      check(got == expectedGot);
      for (size_t byte = 0; byte < got; ++byte)
      {
        uint32_t position = offset + (uint32_t)byte;
        check(piece[byte] == (position < fixture.base[index].size
                                  ? generatedByte(position, fixture.base[index].seed)
                                  : 0x1A));
      }
    }
    size_t got = 99;
    check(guestFsReadAt(volume, 0, fixture.base[index].name, (uint32_t)size, piece, sizeof(piece),
                        &got) == guestFsOk);
    check(got == 0);
  }
  snapshotFree(&shot);
  check(guestFsWrite(volume, 0, "X.Y", 1, generatorSource, &(generatorState){1, 1, 0, 0}, false) ==
        guestFsErrReadOnly);
  closeVolume(volume, &io);

  //-- Adding the same files as the Python tool must give a byte-identical image.
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  for (size_t index = 0; index < fixture.addedCount; ++index)
  {
    check(writeGenerated(volume, 0, fixture.added[index].name, fixture.added[index].size,
                         fixture.added[index].seed, false) == guestFsOk);
  }
  checkInfo(volume, fixture.freeBlocksAfter, fixture.freeSlotsAfter);
  closeVolume(volume, &io);
  check(filesEqual(imagePath, expectedPath));
  char journal[512];
  journalPathFor(journal, sizeof(journal));
  check(!fileExists(journal));

  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  for (size_t index = 0; index < fixture.addedCount; ++index)
  {
    size_t size;
    uint8_t* data = readGuestFile(volume, 0, fixture.added[index].name, &size);
    checkGenerated(data, size, fixture.added[index].size, fixture.added[index].seed);
    free(data);
  }
  //-- Duplicates need an explicit overwrite and lowercase names are normalised.
  check(writeGenerated(volume, 0, fixture.added[1].name, 100, 1, false) == guestFsErrExists);
  char lower[32];
  strcpy(lower, fixture.added[1].name);
  for (char* character = lower; *character != '\0'; ++character)
  {
    if (*character >= 'A' && *character <= 'Z')
    {
      *character = (char)(*character - 'A' + 'a');
    }
  }
  check(writeGenerated(volume, 0, lower, 100, 1, false) == guestFsErrExists);
  closeVolume(volume, &io);
  check(filesEqual(imagePath, expectedPath));
  ++testsRun;
}

static void testEditing(const char* stem)
{
  fixtureInfo fixture;
  char basePath[512];
  char imagePath[512];
  char name[96];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, stem);
  snprintf(name, sizeof(name), "%s.base.dsk", stem);
  joinPath(basePath, sizeof(basePath), fixtureDirectory, name);
  snprintf(name, sizeof(name), "%s.edit.dsk", stem);
  workPath(imagePath, sizeof(imagePath), name);
  copyFile(basePath, imagePath);
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);

  //-- Name and argument validation.
  check(writeGenerated(volume, 0, "TOOLONGNAME.TXT", 10, 1, false) == guestFsErrBadName);
  check(writeGenerated(volume, 0, "BAD NAME.TXT", 10, 1, false) == guestFsErrBadName);
  check(writeGenerated(volume, 0, "A.LONG", 10, 1, false) == guestFsErrBadName);
  check(writeGenerated(volume, 0, "", 10, 1, false) == guestFsErrBadName);
  check(writeGenerated(volume, 0, "A*.TXT", 10, 1, false) == guestFsErrBadName);
  check(writeGenerated(volume, 0, "OK.TXT", 0, 1, false) == guestFsErrEmptyFile);
  check(writeGenerated(volume, 16, "OK.TXT", 10, 1, false) == guestFsErrInvalidArgument);
  guestFsEntry entry;
  check(guestFsStat(volume, 0, "NOPE.TXT", &entry) == guestFsErrNotFound);
  check(guestFsStat(volume, 5, fixture.base[0].name, &entry) == guestFsErrNotFound);
  check(guestFsRemove(volume, 0, "NOPE.TXT") == guestFsErrNotFound);
  check(guestFsRename(volume, 0, "NOPE.TXT", "OTHER.TXT") == guestFsErrNotFound);
  checkInfo(volume, fixture.freeBlocks, fixture.freeSlots);

  //-- A source that fails part-way must leave nothing behind.
  generatorState failing = {5000, 9, 0, 2000};
  check(guestFsWrite(volume, 0, "FAIL.DAT", 5000, generatorSource, &failing, false) ==
        guestFsErrAborted);
  check(guestFsStat(volume, 0, "FAIL.DAT", &entry) == guestFsErrNotFound);
  checkInfo(volume, fixture.freeBlocks, fixture.freeSlots);

  //-- User areas keep identical names apart.
  check(writeGenerated(volume, 3, fixture.base[0].name, 500, 21, false) == guestFsOk);
  size_t size;
  uint8_t* data = readGuestFile(volume, 3, fixture.base[0].name, &size);
  checkGenerated(data, size, 500, 21);
  free(data);
  data = readGuestFile(volume, 0, fixture.base[0].name, &size);
  checkGenerated(data, size, fixture.base[0].size, fixture.base[0].seed);
  free(data);

  //-- Rename keeps the content, refuses clashes, is case-insensitive.
  check(guestFsRename(volume, 3, fixture.base[0].name, fixture.base[1].name) == guestFsOk);
  check(guestFsStat(volume, 3, fixture.base[0].name, &entry) == guestFsErrNotFound);
  check(guestFsRename(volume, 0, fixture.base[0].name, fixture.base[1].name) == guestFsErrExists);
  check(guestFsRename(volume, 0, fixture.base[0].name, "BAD NAME") == guestFsErrBadName);
  check(guestFsRename(volume, 0, fixture.base[0].name, "renamed.txt") == guestFsOk);
  data = readGuestFile(volume, 0, "RENAMED.TXT", &size);
  checkGenerated(data, size, fixture.base[0].size, fixture.base[0].seed);
  free(data);
  data = readGuestFile(volume, 3, fixture.base[1].name, &size);
  checkGenerated(data, size, 500, 21);
  free(data);
  check(guestFsRename(volume, 0, "RENAMED.TXT", "RENAMED.TXT") == guestFsOk);

  //-- Overwrite: new content, other files untouched, no leaked blocks after delete.
  check(writeGenerated(volume, 0, fixture.base[3].name, 777, 33, true) == guestFsOk);
  data = readGuestFile(volume, 0, fixture.base[3].name, &size);
  checkGenerated(data, size, 777, 33);
  free(data);
  data = readGuestFile(volume, 0, fixture.base[4].name, &size);
  checkGenerated(data, size, fixture.base[4].size, fixture.base[4].seed);
  free(data);
  check(guestFsRemove(volume, 0, fixture.base[3].name) == guestFsOk);
  check(guestFsRemove(volume, 0, fixture.base[2].name) == guestFsOk);
  check(guestFsRemove(volume, 3, fixture.base[1].name) == guestFsOk);
  check(guestFsRemove(volume, 0, "RENAMED.TXT") == guestFsOk);
  check(guestFsRemove(volume, 0, fixture.base[4].name) == guestFsOk);
  check(guestFsRemove(volume, 0, fixture.base[1].name) == guestFsOk);
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  check(info.freeBlocks == info.totalBlocks && info.freeDirectoryEntries == info.directoryEntries);
  snapshot shot;
  snapshotTake(&shot, volume);
  check(shot.count == 0);
  snapshotFree(&shot);
  closeVolume(volume, &io);

  //-- An emptied disk is a fully formatted one.
  char emptyPath[512];
  snprintf(name, sizeof(name), "%s.empty.dsk", stem);
  workPath(emptyPath, sizeof(emptyPath), name);
  size_t imageSize;
  uint8_t* image = readFileFully(imagePath, &imageSize);
  for (size_t index = fixture.directoryOffset;
       index < fixture.directoryOffset + (size_t)fixture.directoryEntries * 32; ++index)
  {
    check(image[index] == 0xE5 || (index % 32) != 0);
  }
  free(image);
  ++testsRun;
}

static void testFullAndDirectoryFull(const char* stem)
{
  fixtureInfo fixture;
  char basePath[512];
  char imagePath[512];
  char name[96];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, stem);
  snprintf(name, sizeof(name), "%s.base.dsk", stem);
  joinPath(basePath, sizeof(basePath), fixtureDirectory, name);
  snprintf(name, sizeof(name), "%s.full.dsk", stem);
  workPath(imagePath, sizeof(imagePath), name);
  copyFile(basePath, imagePath);
  size_t beforeSize;
  uint8_t* before = readFileFully(imagePath, &beforeSize);
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  uint32_t tooBig = (fixture.freeBlocks + 1) * fixture.blockSize;
  check(writeGenerated(volume, 0, "HUGE.DAT", tooBig, 1, false) == guestFsErrNoSpace);
  check(writeGenerated(volume, 0, fixture.base[0].name, tooBig, 1, true) == guestFsErrNoSpace);
  closeVolume(volume, &io);
  size_t afterSize;
  uint8_t* after = readFileFully(imagePath, &afterSize);
  check(afterSize == beforeSize && memcmp(before, after, beforeSize) == 0);
  free(after);
  free(before);

  //-- Fill the directory with one-record files, then the disk with large ones.
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  uint32_t created = 0;
  for (uint32_t index = 0; index < fixture.freeSlots; ++index)
  {
    char fileName[24];
    snprintf(fileName, sizeof(fileName), "F%u.TMP", (unsigned)index);
    guestFsStatus status = writeGenerated(volume, 0, fileName, 10, index, false);
    if (status != guestFsOk)
    {
      check(status == guestFsErrNoSpace);
      break;
    }
    ++created;
  }
  guestFsInfo info;
  guestFsInfoGet(volume, &info);
  if (info.freeDirectoryEntries == 0)
  {
    check(writeGenerated(volume, 0, "ONEMORE.TMP", 10, 1, false) == guestFsErrDirectoryFull);
  }
  check(created > 0);
  closeVolume(volume, &io);
  check(openVolume(imagePath, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  closeVolume(volume, &io);
  ++testsRun;
}

static uint8_t* mutatedImage(const fixtureInfo* fixture, size_t* size)
{
  char path[512];
  char name[96];
  snprintf(name, sizeof(name), "%s.base.dsk", fixture->stem);
  joinPath(path, sizeof(path), fixtureDirectory, name);
  return readFileFully(path, size);
}

static guestFsStatus openMutated(const fixtureInfo* fixture, const uint8_t* image, size_t size)
{
  char path[512];
  testIo io;
  guestFsVolume* volume = NULL;
  char detail[guestFsDetailCapacity];
  workPath(path, sizeof(path), "mutated.dsk");
  FILE* file = fopen(path, "wb");
  check(file != NULL);
  check(fwrite(image, 1, size, file) == size);
  fclose(file);
  guestFsStatus status = openVolume(path, fixture, &io, &volume, true, -1, detail, sizeof(detail));
  if (status == guestFsOk)
  {
    closeVolume(volume, &io);
  }
  else
  {
    check(detail[0] != '\0');
  }
  return status;
}

static void testCorruptImages(const char* stem)
{
  fixtureInfo fixture;
  size_t size;
  loadFixture(&fixture, stem);
  uint8_t* image = mutatedImage(&fixture, &size);
  check(openMutated(&fixture, image, size) == guestFsOk);
  uint8_t* entry0 = image + fixture.directoryOffset;
  uint8_t* entry1 = entry0 + 32;
  uint8_t saved[32];
  memcpy(saved, entry0, 32);
  uint8_t savedOne[32];
  memcpy(savedOne, entry1, 32);

  entry0[0] = 0x20;
  check(openMutated(&fixture, image, size) == guestFsErrCorrupt);
  memcpy(entry0, saved, 32);
  entry0[1] = '*';
  check(openMutated(&fixture, image, size) == guestFsErrCorrupt);
  memcpy(entry0, saved, 32);
  memset(entry0 + 1, ' ', 8);
  check(openMutated(&fixture, image, size) == guestFsErrCorrupt);
  memcpy(entry0, saved, 32);
  entry0[15] = 129;
  check(openMutated(&fixture, image, size) == guestFsErrCorrupt);
  memcpy(entry0, saved, 32);
  entry0[16] = 0;
  entry0[17] = 0;
  check(openMutated(&fixture, image, size) == guestFsErrCorrupt);
  memcpy(entry0, saved, 32);
  entry0[16] = (uint8_t)((fixture.maxBlock + 1) & 0xFF);
  entry0[17] = (uint8_t)((fixture.maxBlock + 1) >> 8);
  check(openMutated(&fixture, image, size) == guestFsErrCorrupt);
  memcpy(entry0, saved, 32);
  //-- Second entry pointing at the first entry's block.
  memcpy(entry1 + 16, saved + 16, fixture.maxBlock > 255 ? 2 : 1);
  check(openMutated(&fixture, image, size) == guestFsErrCorrupt);
  memcpy(entry1, savedOne, 32);
  //-- Same user, name and extent twice.
  memcpy(entry1, saved, 32);
  entry1[16] = 0;
  entry1[17] = 0;
  check(openMutated(&fixture, image, size) == guestFsErrCorrupt);
  memcpy(entry1, savedOne, 32);
  check(openMutated(&fixture, image, size) == guestFsOk);

  //-- Unknown geometry: wrong image size and unknown OS.
  check(openMutated(&fixture, image, size - 128) == guestFsErrUnsupported);
  free(image);
  char mutatedPath[512];
  workPath(mutatedPath, sizeof(mutatedPath), "mutated.dsk");
  remove(mutatedPath);
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

static guestFsStatus runScenario(guestFsVolume* volume, scenarioKind kind,
                                 const fixtureInfo* fixture)
{
  switch (kind)
  {
  case scenarioOverwrite:
    return writeGenerated(volume, 0, fixture->added[0].name, fixture->added[0].size + 9000, 55,
                          true);
  case scenarioDelete:
    return guestFsRemove(volume, 0, fixture->added[0].name);
  case scenarioRename:
    return guestFsRename(volume, 0, fixture->added[0].name, "MOVED.DAT");
  case scenarioAdd:
    return writeGenerated(volume, 0, "BRANDNEW.DAT", fixture->added[0].size, 66, false);
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
  guestFsStatus status = runScenario(volume, kind, fixture);
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
  //-- The recovered image must still be a valid, writable CP/M disk.
  check(openVolume(imagePath, fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, 0, "AFTER.TMP", 300, 77, false) == guestFsOk);
  closeVolume(volume, &io);
}

static void testCrashRecovery(const char* stem, scenarioKind kind)
{
  fixtureInfo fixture;
  char expectedPath[512];
  char name[96];
  testIo io;
  guestFsVolume* volume;
  loadFixture(&fixture, stem);
  snprintf(name, sizeof(name), "%s.expected.dsk", stem);
  joinPath(expectedPath, sizeof(expectedPath), fixtureDirectory, name);
  guestFsFaultHook = faultHook;

  check(openVolume(expectedPath, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  snapshot oldShot;
  snapshotTake(&oldShot, volume);
  closeVolume(volume, &io);

  //-- Dry run: count I/O operations and build the "new" snapshot.
  char imagePath[512];
  workPath(imagePath, sizeof(imagePath), "dry.dsk");
  copyFile(expectedPath, imagePath);
  check(openVolume(imagePath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  hook.target = NULL;
  check(runScenario(volume, kind, &fixture) == guestFsOk);
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
  loadFixture(&fixture, "cpm80-SYSTEM");
  journalPathFor(journal, sizeof(journal));
  joinPath(basePath, sizeof(basePath), fixtureDirectory, "cpm80-SYSTEM.base.dsk");
  workPath(imagePath, sizeof(imagePath), "garbage.dsk");
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
  check(guestFsJournalBegin(&made, journal, "cpm80-SYSTEM", io.size) == guestFsOk);
  check(guestFsJournalSave(&made, &guest, 6656, 32) == guestFsOk);
  check(guestFsJournalSeal(&made) == guestFsOk);
  guestFsJournalAbandon(&made);
  check(guestFsJournalPeek(journal, identity, sizeof(identity)) == guestFsOk &&
        strcmp(identity, "cpm80-SYSTEM") == 0);
  FILE* damage = fopen(journal, "r+b");
  check(damage != NULL);
  check(fseek(damage, 148 + 20, SEEK_SET) == 0);
  fputc(0x55, damage);
  fclose(damage);
  check(guestFsJournalRecover(journal, &guest) == guestFsErrCorrupt);
  check(fileExists(journal));
  remove(journal);

  //-- A journal for a differently sized image is not applied.
  check(guestFsJournalBegin(&made, journal, "cpm80-SYSTEM", io.size + 128) == guestFsOk);
  check(guestFsJournalSave(&made, &guest, 6656, 32) == guestFsOk);
  check(guestFsJournalSeal(&made) == guestFsOk);
  guestFsJournalAbandon(&made);
  check(guestFsJournalRecover(journal, &guest) == guestFsErrCorrupt);
  check(fileExists(journal));
  remove(journal);
  ioClose(&io);
  remove(imagePath);
  ++testsRun;
}

typedef struct
{
  int polls;
  int cancelAfter;
} pollState;

static bool pollCancel(void* context)
{
  pollState* state = context;
  return ++state->polls > state->cancelAfter;
}

static void testCopyAndFiles(void)
{
  fixtureInfo source;
  fixtureInfo destination;
  char sourcePath[512];
  char destinationPath[512];
  char basePath[512];
  testIo sourceIo;
  testIo destinationIo;
  guestFsVolume* sourceVolume;
  guestFsVolume* destinationVolume;
  loadFixture(&source, "cpm80-BIG");
  loadFixture(&destination, "cpm86-LARGE");
  joinPath(basePath, sizeof(basePath), fixtureDirectory, "cpm80-BIG.expected.dsk");
  workPath(sourcePath, sizeof(sourcePath), "copySource.dsk");
  copyFile(basePath, sourcePath);
  joinPath(basePath, sizeof(basePath), fixtureDirectory, "cpm86-LARGE.base.dsk");
  workPath(destinationPath, sizeof(destinationPath), "copyDestination.dsk");
  copyFile(basePath, destinationPath);
  check(openVolume(sourcePath, &source, &sourceIo, &sourceVolume, false, -1, NULL, 0) == guestFsOk);
  check(openVolume(destinationPath, &destination, &destinationIo, &destinationVolume, false, -1,
                   NULL, 0) == guestFsOk);

  //-- Image to image (different OS), including a multi-extent source.
  check(guestFsCopy(sourceVolume, 0, "MULTI.DAT", destinationVolume, 0, "COPY.DAT", false, NULL,
                    NULL) == guestFsOk);
  size_t size;
  uint8_t* data = readGuestFile(destinationVolume, 0, "COPY.DAT", &size);
  checkGenerated(data, size, 300000, 6);
  free(data);
  check(guestFsCopy(sourceVolume, 0, "MULTI.DAT", destinationVolume, 0, "COPY.DAT", false, NULL,
                    NULL) == guestFsErrExists);
  //-- Overwrite keeps the old blocks until the commit, so it needs room for both copies.
  check(guestFsCopy(sourceVolume, 0, "MULTI.DAT", destinationVolume, 0, "COPY.DAT", true, NULL,
                    NULL) == guestFsErrNoSpace);
  data = readGuestFile(destinationVolume, 0, "COPY.DAT", &size);
  checkGenerated(data, size, 300000, 6);
  free(data);
  check(guestFsCopy(sourceVolume, 0, "NEW.COM", destinationVolume, 0, "COPY.DAT", true, NULL,
                    NULL) == guestFsOk);
  data = readGuestFile(destinationVolume, 0, "COPY.DAT", &size);
  checkGenerated(data, size, 3000, 7);
  free(data);
  check(guestFsCopy(sourceVolume, 0, "MULTI.DAT", destinationVolume, 0, "COPY.DAT", true, NULL,
                    NULL) == guestFsOk);
  check(guestFsCopy(sourceVolume, 0, "NOPE.DAT", destinationVolume, 0, "X.DAT", false, NULL,
                    NULL) == guestFsErrNotFound);
  //-- Copy inside one volume (other user area) and onto itself.
  check(guestFsCopy(sourceVolume, 0, "NEW.COM", sourceVolume, 2, "NEW.COM", false, NULL, NULL) ==
        guestFsOk);
  check(guestFsCopy(sourceVolume, 0, "NEW.COM", sourceVolume, 0, "new.com", true, NULL, NULL) ==
        guestFsErrInvalidArgument);
  data = readGuestFile(sourceVolume, 2, "NEW.COM", &size);
  checkGenerated(data, size, 3000, 7);
  free(data);

  //-- Cancellation leaves the destination unchanged.
  check(guestFsRemove(destinationVolume, 0, "COPY.DAT") == guestFsOk);
  snapshot before;
  snapshotTake(&before, destinationVolume);
  pollState cancel = {0, 3};
  check(guestFsCopy(sourceVolume, 0, "MULTI.DAT", destinationVolume, 0, "CANCEL.DAT", false,
                    pollCancel, &cancel) == guestFsErrAborted);
  snapshot after;
  snapshotTake(&after, destinationVolume);
  check(snapshotEqual(&before, &after));
  snapshotFree(&before);
  snapshotFree(&after);

  //-- Export to and import from ordinary files.
  char hostPath[512];
  workPath(hostPath, sizeof(hostPath), "exported.bin");
  FILE* output = fopen(hostPath, "wb");
  check(output != NULL);
  check(guestFsExportToFile(sourceVolume, 0, "MULTI.DAT", output, NULL, NULL) == guestFsOk);
  fclose(output);
  uint8_t* exported = readFileFully(hostPath, &size);
  checkGenerated(exported, size, 300000, 6);
  free(exported);
  FILE* input = fopen(hostPath, "rb");
  check(input != NULL);
  check(guestFsImportFromFile(sourceVolume, 1, "IMPORT.BIN", input, (uint32_t)size, false, NULL,
                              NULL) == guestFsOk);
  fclose(input);
  data = readGuestFile(sourceVolume, 1, "IMPORT.BIN", &size);
  checkGenerated(data, size, 300000, 6);
  free(data);
  input = fopen(hostPath, "rb");
  check(guestFsImportFromFile(sourceVolume, 1, "IMPORT.BIN", input, 1000000, true, NULL, NULL) ==
        guestFsErrIo);
  fclose(input);
  closeVolume(sourceVolume, &sourceIo);
  closeVolume(destinationVolume, &destinationIo);
  ++testsRun;
}

//-- The firmware binding (main/guestFsImage.c over main/imageFile.c) must behave like testIo.
static void testImageFileBinding(void)
{
  char basePath[512];
  char expectedPath[512];
  char imagePath[512];
  char journal[512];
  imageFile image = {0};
  guestFsIo guest;
  guestFsVolume* volume = NULL;
  fixtureInfo fixture;
  loadFixture(&fixture, "cpm80-LARGE");
  joinPath(basePath, sizeof(basePath), fixtureDirectory, "cpm80-LARGE.base.dsk");
  joinPath(expectedPath, sizeof(expectedPath), fixtureDirectory, "cpm80-LARGE.expected.dsk");
  workPath(imagePath, sizeof(imagePath), "binding.dsk");
  journalPathFor(journal, sizeof(journal));
  copyFile(basePath, imagePath);
  check(imageOpen(&image, imagePath, true));
  guestFsImageBind(&guest, &image);
  check(guest.readOnly && guest.size == image.size);
  check(guestFsOpen(&guest, "cpm80", journal, "cpm80/binding.dsk", &volume, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, 0, "X.Y", 5, 1, false) == guestFsErrReadOnly);
  guestFsClose(volume);
  check(imageClose(&image));
  memset(&image, 0, sizeof(image));
  check(imageOpen(&image, imagePath, false));
  guestFsImageBind(&guest, &image);
  check(guestFsOpen(&guest, "cpm80", journal, "cpm80/binding.dsk", &volume, NULL, 0) == guestFsOk);
  for (size_t index = 0; index < fixture.addedCount; ++index)
  {
    check(writeGenerated(volume, 0, fixture.added[index].name, fixture.added[index].size,
                         fixture.added[index].seed, false) == guestFsOk);
  }
  guestFsClose(volume);
  check(imageClose(&image));
  check(filesEqual(imagePath, expectedPath));
  ++testsRun;
}

//-- Read the real sdcard images and compare with the files the Python tools extract.
static void compareRealImage(const char* osId, const char* imageName)
{
  fixtureInfo fixture;
  char path[512];
  char dumpDirectory[640];
  char name[160];
  testIo io;
  guestFsVolume* volume;
  memset(&fixture, 0, sizeof(fixture));
  strcpy(fixture.os, osId);
  snprintf(fixture.stem, sizeof(fixture.stem), "real-%s", osId);
  snprintf(name, sizeof(name), "retro/images/%s/%s.dsk", osId, imageName);
  snprintf(path, sizeof(path), "%s/sdcard/%s", projectRoot, name);
  if (!fileExists(path))
  {
    return;
  }
  check(openVolume(path, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  snprintf(dumpDirectory, sizeof(dumpDirectory), "%s/real-%s-%s", fixtureDirectory, osId,
           imageName);
  snapshot shot;
  snapshotTake(&shot, volume);
  size_t compared = 0;
  for (size_t index = 0; index < shot.count; ++index)
  {
    char expectedPath[900];
    snprintf(expectedPath, sizeof(expectedPath), "%s/%u-%s", dumpDirectory, shot.files[index].user,
             shot.files[index].name);
    size_t expectedSize;
    uint8_t* expected = readFileFully(expectedPath, &expectedSize);
    check(expectedSize == shot.files[index].size &&
          memcmp(expected, shot.files[index].data, expectedSize) == 0);
    free(expected);
    ++compared;
  }
  DIR* directory = opendir(dumpDirectory);
  check(directory != NULL);
  size_t expectedFiles = 0;
  for (struct dirent* item = readdir(directory); item != NULL; item = readdir(directory))
  {
    if (item->d_name[0] != '.' && strcmp(item->d_name, "free.txt") != 0)
    {
      ++expectedFiles;
    }
  }
  closedir(directory);
  check(expectedFiles == compared);
  char freePath[900];
  snprintf(freePath, sizeof(freePath), "%s/free.txt", dumpDirectory);
  FILE* freeFile = fopen(freePath, "r");
  check(freeFile != NULL);
  unsigned freeBlocks;
  unsigned freeSlots;
  check(fscanf(freeFile, "%u %u", &freeBlocks, &freeSlots) == 2);
  fclose(freeFile);
  checkInfo(volume, freeBlocks, freeSlots);
  snapshotFree(&shot);
  closeVolume(volume, &io);
  printf("  real image %s/%s: %zu files identical to Python\n", osId, imageName, compared);
}

//-- Write a file into a copy of a real image, then export the whole state for Python.
static void roundTripRealImage(const char* osId, const char* imageName)
{
  fixtureInfo fixture;
  char path[512];
  char copyPath[512];
  char name[160];
  testIo io;
  guestFsVolume* volume;
  memset(&fixture, 0, sizeof(fixture));
  strcpy(fixture.os, osId);
  snprintf(fixture.stem, sizeof(fixture.stem), "real-%s", osId);
  snprintf(path, sizeof(path), "%s/sdcard/retro/images/%s/%s.dsk", projectRoot, osId, imageName);
  if (!fileExists(path))
  {
    return;
  }
  snprintf(name, sizeof(name), "%s-real-%s.dsk", osId, imageName);
  workPath(copyPath, sizeof(copyPath), name);
  copyFile(path, copyPath);
  check(openVolume(copyPath, &fixture, &io, &volume, false, -1, NULL, 0) == guestFsOk);
  check(writeGenerated(volume, 0, "GFSTEST.BIN", 5000, 99, false) == guestFsOk);
  check(writeGenerated(volume, 0, "GFSTEST.BIN", 3000, 98, true) == guestFsOk);
  check(guestFsRename(volume, 0, "GFSTEST.BIN", "GFSRENAM.BIN") == guestFsOk);
  closeVolume(volume, &io);
  check(openVolume(copyPath, &fixture, &io, &volume, true, -1, NULL, 0) == guestFsOk);
  size_t size;
  uint8_t* data = readGuestFile(volume, 0, "GFSRENAM.BIN", &size);
  checkGenerated(data, size, 3000, 98);
  free(data);
  dumpVolumeFiles(volume, name);
  closeVolume(volume, &io);
}

int main(int argc, char** argv)
{
  static const char* const stems[] = {"cpm80-SYSTEM", "cpm80-LARGE", "cpm80-BIG",
                                      "cpm86-CPM86",  "cpm86-LARGE", "cpm86-BIG"};
  static const char* const crashStems[] = {"cpm80-SYSTEM", "cpm86-LARGE", "cpm80-BIG"};
  if (argc != 4)
  {
    fprintf(stderr, "usage: guestFsTests <fixtureDirectory> <outputDirectory> <projectRoot>\n");
    return 2;
  }
  fixtureDirectory = argv[1];
  outputDirectory = argv[2];
  projectRoot = argv[3];
  mkdir(outputDirectory, 0755);

  check(guestFsSupportsOs("cpm80") && guestFsSupportsOs("cpm86") && !guestFsSupportsOs("apple2"));
  for (size_t index = 0; index < sizeof(stems) / sizeof(stems[0]); ++index)
  {
    testBaseAndPythonParity(stems[index]);
    testEditing(stems[index]);
    testFullAndDirectoryFull(stems[index]);
    testCorruptImages(stems[index]);
    printf("PASS: guest CP/M %s\n", stems[index]);
  }
  for (size_t index = 0; index < sizeof(crashStems) / sizeof(crashStems[0]); ++index)
  {
    for (int kind = scenarioOverwrite; kind <= scenarioAdd; ++kind)
    {
      testCrashRecovery(crashStems[index], (scenarioKind)kind);
    }
    printf("PASS: crash recovery %s\n", crashStems[index]);
  }
  testJournalGarbage();
  testCopyAndFiles();
  testImageFileBinding();
  static const char* const realCpm80[] = {"languages", "Utility", "work1", "work2"};
  static const char* const realCpm86[] = {"TurboP", "Utility", "work86"};
  for (size_t index = 0; index < sizeof(realCpm80) / sizeof(realCpm80[0]); ++index)
  {
    compareRealImage("cpm80", realCpm80[index]);
    roundTripRealImage("cpm80", realCpm80[index]);
  }
  for (size_t index = 0; index < sizeof(realCpm86) / sizeof(realCpm86[0]); ++index)
  {
    compareRealImage("cpm86", realCpm86[index]);
    roundTripRealImage("cpm86", realCpm86[index]);
  }
  printf("PASS: guestFs tests (%d groups)\n", testsRun);
  return 0;
}
