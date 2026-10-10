#include "apple2Core.h"
#include "apple2Disk.h"
#include "apple2DiskImage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//-- Disk II (slot 6, drive 1, DOS 3.3 16-sector) host tests, including writable media.
//-- Synthetic data is used for the low-level tests; the generated system.dsk is used for the DOS tests.

enum
{
  imageBytes = apple2DiskImageSize
};

static const uint8_t testGcrWriteTable[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

static uint8_t patternByte(uint8_t track, uint8_t sector, size_t offset)
{
  return (uint8_t)(track * 31U + sector * 7U + offset * 13U + (offset >> 3));
}

static void fillPatternImage(uint8_t *image)
{
  for (uint8_t track = 0; track < apple2DiskImageTracks; ++track)
  {
    for (uint8_t sector = 0; sector < apple2DiskImageSectorsPerTrack; ++sector)
    {
      uint8_t *target = &image[((size_t)track * apple2DiskImageSectorsPerTrack + sector) * apple2DiskImageSectorSize];
      for (size_t offset = 0; offset < apple2DiskImageSectorSize; ++offset)
      {
        target[offset] = patternByte(track, sector, offset);
      }
    }
  }
}

static void encodeWriteData(const uint8_t *data, uint8_t *nibbles)
{
  uint8_t values[342] = {0};
  for (size_t index = 0; index < apple2DiskSectorSize; ++index)
  {
    uint8_t low = (uint8_t)(data[index] & 3U);
    uint8_t reversed = (uint8_t)(((low & 1U) << 1) | (low >> 1));
    values[index % 86] |= (uint8_t)(reversed << (2 * (index / 86)));
    values[86 + index] = (uint8_t)(data[index] >> 2);
  }
  uint8_t previous = 0;
  for (size_t index = 0; index < sizeof(values); ++index)
  {
    nibbles[index] = testGcrWriteTable[values[index] ^ previous];
    previous = values[index];
  }
  nibbles[sizeof(values)] = testGcrWriteTable[previous];
}

static void writeDiskDataField(apple2Disk *controller, const uint8_t *encoded, uint64_t firstNibbleCycle)
{
  const uint8_t prologue[] = {0xD5, 0xAA, 0xAD};
  for (size_t index = 0; index < sizeof(prologue) + apple2DiskWriteDataSize; ++index)
  {
    uint8_t value = index < sizeof(prologue) ? prologue[index] : encoded[index - sizeof(prologue)];
    uint64_t cycles = firstNibbleCycle + index * apple2DiskCyclesPerNibble;
    apple2DiskAccess(controller, 0xD, true, cycles - 1, value);
    apple2DiskAccess(controller, 0xC, false, cycles, 0);
  }
}

static void makeTempPath(char *path, size_t size)
{
  snprintf(path, size, "/tmp/apple2DiskTestXXXXXX");
  int descriptor = mkstemp(path);
  assert(descriptor >= 0);
  assert(close(descriptor) == 0);
}

static void writeFile(const char *path, const uint8_t *data, size_t size)
{
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  assert(fwrite(data, 1, size, file) == size);
  assert(fclose(file) == 0);
}

static uint8_t *readWholeFile(const char *path, size_t *size)
{
  FILE *file = fopen(path, "rb");
  assert(file != NULL);
  assert(fseek(file, 0, SEEK_END) == 0);
  long length = ftell(file);
  assert(length > 0);
  assert(fseek(file, 0, SEEK_SET) == 0);
  uint8_t *data = malloc((size_t)length);
  assert(data != NULL);
  assert(fread(data, 1, (size_t)length, file) == (size_t)length);
  assert(fclose(file) == 0);
  *size = (size_t)length;
  return data;
}

static void testImageOpenAndBounds(void)
{
  apple2DiskImage disk;
  apple2DiskImageInitialize(&disk);
  uint8_t buffer[apple2DiskImageSectorSize];
  assert(apple2DiskImageOpen(NULL, "x") == apple2DiskImageInvalidArgument);
  assert(apple2DiskImageOpen(&disk, NULL) == apple2DiskImageInvalidArgument);
  assert(apple2DiskImageOpen(&disk, "/tmp/apple2DiskTest-does-not-exist") == apple2DiskImageNotFound);
  assert(apple2DiskImageOpen(&disk, "/tmp") == apple2DiskImageNotFound);
  assert(!apple2DiskImageIsOpen(&disk));
  assert(apple2DiskImageReadSector(&disk, 0, 0, buffer) == apple2DiskImageNotOpen);
  assert(apple2DiskImageClose(&disk) == apple2DiskImageNotOpen);

  char path[64];
  makeTempPath(path, sizeof(path));
  uint8_t *image = malloc(imageBytes + 1);
  assert(image != NULL);
  memset(image, 0, imageBytes + 1);

  writeFile(path, image, 0);
  assert(apple2DiskImageOpen(&disk, path) == apple2DiskImageBadSize);
  writeFile(path, image, 116480);
  assert(apple2DiskImageOpen(&disk, path) == apple2DiskImageBadSize);
  writeFile(path, image, imageBytes - 1);
  assert(apple2DiskImageOpen(&disk, path) == apple2DiskImageBadSize);
  writeFile(path, image, imageBytes + 1);
  assert(apple2DiskImageOpen(&disk, path) == apple2DiskImageBadSize);
  assert(!apple2DiskImageIsOpen(&disk));

  fillPatternImage(image);
  writeFile(path, image, imageBytes);
  assert(apple2DiskImageOpen(&disk, path) == apple2DiskImageOk);
  assert(apple2DiskImageIsOpen(&disk));
  assert(apple2DiskImageOpen(&disk, path) == apple2DiskImageAlreadyOpen);

  for (uint8_t track = 0; track < apple2DiskImageTracks; ++track)
  {
    for (uint8_t sector = 0; sector < apple2DiskImageSectorsPerTrack; ++sector)
    {
      assert(apple2DiskImageReadSector(&disk, track, sector, buffer) == apple2DiskImageOk);
      for (size_t offset = 0; offset < sizeof(buffer); ++offset)
      {
        assert(buffer[offset] == patternByte(track, sector, offset));
      }
      assert(apple2DiskImageReadSectorCallback(&disk, track, sector, buffer));
    }
  }
  assert(apple2DiskImageReadSector(&disk, apple2DiskImageTracks, 0, buffer) == apple2DiskImageOutOfRange);
  assert(apple2DiskImageReadSector(&disk, 0, apple2DiskImageSectorsPerTrack, buffer) ==
         apple2DiskImageOutOfRange);
  assert(apple2DiskImageReadSector(&disk, 255, 255, buffer) == apple2DiskImageOutOfRange);
  assert(!apple2DiskImageReadSectorCallback(&disk, apple2DiskImageTracks, 0, buffer));
  assert(apple2DiskImageReadSector(&disk, 0, 0, NULL) == apple2DiskImageInvalidArgument);

  //-- Read-only: the underlying handle refuses writes and the file stays byte-identical.
  assert(!imageWriteAt(&disk.image, 0, buffer, sizeof(buffer)));
  assert(apple2DiskImageClose(&disk) == apple2DiskImageOk);
  assert(!apple2DiskImageIsOpen(&disk));
  assert(apple2DiskImageReadSector(&disk, 0, 0, buffer) == apple2DiskImageNotOpen);
  size_t afterSize;
  uint8_t *after = readWholeFile(path, &afterSize);
  assert(afterSize == imageBytes && memcmp(after, image, imageBytes) == 0);
  free(after);

  for (apple2DiskImageResult result = apple2DiskImageOk; result <= apple2DiskImageNotOpen; ++result)
  {
    assert(strlen(apple2DiskImageResultText(result)) > 0);
  }
  free(image);
  assert(unlink(path) == 0);
}

typedef struct
{
  uint8_t data[2][apple2SmartPortBlockSize];
  uint32_t writes;
} smartPortFixture;

static bool smartPortRead(void *context, uint32_t block, uint8_t *buffer)
{
  smartPortFixture *fixture = context;
  if (block >= 2)
  {
    return false;
  }
  memcpy(buffer, fixture->data[block], apple2SmartPortBlockSize);
  return true;
}

static bool smartPortWrite(void *context, uint32_t block, const uint8_t *buffer)
{
  smartPortFixture *fixture = context;
  if (block >= 2)
  {
    return false;
  }
  memcpy(fixture->data[block], buffer, apple2SmartPortBlockSize);
  fixture->writes++;
  return true;
}

static bool unusedDiskSectorRead(void *context, uint8_t track, uint8_t sector, uint8_t *buffer)
{
  (void)context;
  (void)track;
  (void)sector;
  memset(buffer, 0, apple2DiskImageSectorSize);
  return true;
}

static void testSmartPortBlockCall(void)
{
  apple2Core *core = NULL;
  smartPortFixture fixture = {0};
  uint8_t rom[apple2RomSize];
  memset(rom, 0xEA, sizeof(rom));
  rom[0] = 0x4C;
  rom[1] = 0x00;
  rom[2] = 0x08;
  rom[0x2FFC] = 0x00;
  rom[0x2FFD] = 0xD0;
  const uint8_t program[] = {0x20, 0x0D, 0xC5, 0x01, 0x00, 0x03, 0x8D, 0x00, 0x06,
                             0x20, 0x0D, 0xC5, 0x02, 0x00, 0x03, 0x8D, 0x01, 0x06,
                             0x08, 0x68, 0x8D, 0x02, 0x06, 0x4C, 0x17, 0x08};
  for (size_t offset = 0; offset < sizeof(fixture.data[1]); ++offset)
  {
    fixture.data[1][offset] = (uint8_t)(offset * 17U + 3U);
  }

  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 1, 2, smartPortRead, smartPortWrite, &fixture) == apple2CoreOk);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);
  uint8_t signature;
  assert(apple2CoreReadMemory(core, 0xC501, &signature) == apple2CoreOk && signature == 0x20);
  assert(apple2CoreReadMemory(core, 0xC5FE, &signature) == apple2CoreOk && signature == 0xC7);
  assert(apple2CoreReadMemory(core, 0xC5FF, &signature) == apple2CoreOk && signature == 0x0A);
  assert(apple2CoreReadMemory(core, 0xC503, &signature) == apple2CoreOk && signature == 0x00);
  assert(apple2CoreReadMemory(core, 0xC505, &signature) == apple2CoreOk && signature == 0x03);
  assert(apple2CoreReadMemory(core, 0xC507, &signature) == apple2CoreOk && signature == 0x00);
  for (size_t index = 0; index < sizeof(program); ++index)
  {
    assert(apple2CoreWriteMemory(core, (uint16_t)(0x0800 + index), program[index]) == apple2CoreOk);
  }

  const uint8_t parameters[] = {3, 1, 0x00, 0x04, 1, 0, 0};
  const uint8_t blockData[apple2SmartPortBlockSize] = {0};
  for (size_t index = 0; index < sizeof(parameters); ++index)
  {
    assert(apple2CoreWriteMemory(core, (uint16_t)(0x0300 + index), parameters[index]) == apple2CoreOk);
  }
  for (size_t index = 0; index < sizeof(blockData); ++index)
  {
    assert(apple2CoreWriteMemory(core, (uint16_t)(0x0400 + index), blockData[index]) == apple2CoreOk);
  }
  assert(apple2CoreRunCycles(core, 1000) == apple2CoreOk);
  uint8_t result;
  assert(apple2CoreReadMemory(core, 0x0600, &result) == apple2CoreOk && result == 0);
  assert(apple2CoreReadMemory(core, 0x0601, &result) == apple2CoreOk && result == 0);
  assert(apple2CoreReadMemory(core, 0x0602, &result) == apple2CoreOk && (result & 1U) == 0);
  assert(fixture.writes == 1);
  for (size_t index = 0; index < apple2SmartPortBlockSize; ++index)
  {
    assert(apple2CoreReadMemory(core, (uint16_t)(0x0400 + index), &result) == apple2CoreOk);
    assert(result == (uint8_t)(index * 17U + 3U));
    assert(fixture.data[1][index] == (uint8_t)(index * 17U + 3U));
  }

  assert(apple2CoreDetachSmartPortDevice(core, 1) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 1, 2, smartPortRead, NULL, &fixture) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0xC5FE, &signature) == apple2CoreOk && signature == 0xC3);
  assert(apple2CoreReset(core) == apple2CoreOk);
  assert(apple2CoreRunCycles(core, 1000) == apple2CoreOk);
  uint8_t writeResult;
  assert(apple2CoreReadMemory(core, 0x0601, &writeResult) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0x0600, &result) == apple2CoreOk && result == 0);
  assert(writeResult == 0x2B);
  assert(apple2CoreReadMemory(core, 0x0602, &result) == apple2CoreOk && (result & 1U) != 0);
  assert(fixture.writes == 1);

  assert(apple2CoreWriteMemory(core, 0x0304, 2) == apple2CoreOk);
  assert(apple2CoreReset(core) == apple2CoreOk);
  assert(apple2CoreRunCycles(core, 1000) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0x0600, &result) == apple2CoreOk && result == 0x2D);
  assert(apple2CoreReadMemory(core, 0x0601, &writeResult) == apple2CoreOk);
  assert(writeResult == 0x2D);
  assert(apple2CoreReadMemory(core, 0x0602, &result) == apple2CoreOk && (result & 1U) != 0);
  assert(fixture.writes == 1);

  assert(apple2CoreAttachDiskDrive(core, apple2SmartPortSlot, 0, apple2DiskSectorOrderDos,
                                   apple2DiskImageTracks, unusedDiskSectorRead, &fixture) == apple2CoreInvalidArgument);
  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  assert(apple2CoreAttachDiskDrive(core, apple2SmartPortSlot, 0, apple2DiskSectorOrderDos,
                                   apple2DiskImageTracks, unusedDiskSectorRead, &fixture) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 1, 2, smartPortRead, NULL, &fixture) ==
         apple2CoreInvalidArgument);
  apple2CoreDestroy(core);
}

static void testWritableDiskImage(void)
{
  char path[64];
  makeTempPath(path, sizeof(path));
  size_t imageSize = apple2DiskImage640kSize;
  uint8_t *blank = calloc(imageSize, 1);
  assert(blank != NULL);
  writeFile(path, blank, imageSize);

  apple2DiskImage image;
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfileMode(&image, path, apple2DiskImageProfile640k,
                                        apple2DiskImageOrderProdos, false) == apple2DiskImageOk);
  uint8_t expected[apple2DiskImageSectorSize];
  for (size_t offset = 0; offset < sizeof(expected); ++offset)
  {
    expected[offset] = (uint8_t)(offset * 29U + 7U);
  }

  apple2Disk controller;
  apple2DiskInitialize(&controller);
  assert(apple2DiskAttachWritableDrive(&controller, 0, apple2DiskSectorOrderProdos,
                                       apple2DiskImage640kTracks, apple2DiskImageReadSectorCallback,
                                       apple2DiskImageWriteSectorCallback, &image));
  controller.drives[0].halfTrack = (uint16_t)(2 * (apple2DiskImage640kTracks - 1));
  apple2DiskAccess(&controller, 0x9, false, 0, 0);
  apple2DiskAccess(&controller, 0xF, true, 0, 0);
  uint8_t encoded[apple2DiskWriteDataSize];
  encodeWriteData(expected, encoded);
  uint64_t firstNibbleCycle =
      (uint64_t)(apple2DiskNibblesPerSector + 60) * apple2DiskCyclesPerNibble;
  writeDiskDataField(&controller, encoded, firstNibbleCycle);
  assert(controller.writeAttempts == 1 && controller.writeFailures == 0);

  uint8_t malformed[apple2DiskWriteDataSize];
  memcpy(malformed, encoded, sizeof(malformed));
  malformed[sizeof(malformed) - 1] = 0;
  uint64_t secondSectorFirstNibbleCycle =
      (uint64_t)(2 * apple2DiskNibblesPerSector + 60) * apple2DiskCyclesPerNibble;
  writeDiskDataField(&controller, malformed, secondSectorFirstNibbleCycle);
  assert(controller.writeAttempts == 2 && controller.writeFailures == 1);
  apple2DiskDetach(&controller);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);

  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, path, apple2DiskImageProfile640k,
                                    apple2DiskImageOrderProdos) == apple2DiskImageOk);
  uint8_t actual[apple2DiskImageSectorSize];
  assert(apple2DiskImageReadSector(&image, apple2DiskImage640kTracks - 1, 8, actual) == apple2DiskImageOk);
  assert(memcmp(actual, expected, sizeof(expected)) == 0);
  assert(apple2DiskImageReadSector(&image, apple2DiskImage640kTracks - 1, 1, actual) == apple2DiskImageOk);
  for (size_t offset = 0; offset < sizeof(actual); ++offset)
  {
    assert(actual[offset] == 0);
  }
  assert(apple2DiskImageWriteSector(&image, apple2DiskImage640kTracks - 1, 8, expected) ==
         apple2DiskImageWriteFailed);

  apple2DiskInitialize(&controller);
  assert(apple2DiskAttachDrive(&controller, 0, apple2DiskSectorOrderProdos, apple2DiskImage640kTracks,
                               apple2DiskImageReadSectorCallback, &image));
  controller.drives[0].halfTrack = (uint16_t)(2 * (apple2DiskImage640kTracks - 1));
  apple2DiskAccess(&controller, 0x9, false, 0, 0);
  apple2DiskAccess(&controller, 0xF, true, 0, 0);
  writeDiskDataField(&controller, encoded, firstNibbleCycle);
  assert(controller.writeAttempts == 1 && controller.writeFailures == 1);
  apple2DiskDetach(&controller);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
  free(blank);
  assert(unlink(path) == 0);
}

//-- Independent description of a valid 6-and-2 disk byte: bit 7 set, at least one pair of adjacent ones
//-- below bit 7, and at most one pair of adjacent zero bits.
static bool isValidDiskByte(unsigned value)
{
  if ((value & 0x80U) == 0 || (value & (value >> 1) & 0x3FU) == 0)
  {
    return false;
  }
  unsigned zeroPairs = ~(value | (value >> 1)) & 0x7FU;
  return (zeroPairs & (zeroPairs - 1U)) == 0;
}

static int decodeTable[256];

static void buildDecodeTable(void)
{
  int count = 0;
  for (int index = 0; index < 256; ++index)
  {
    decodeTable[index] = -1;
  }
  for (unsigned value = 0x80; value <= 0xFF; ++value)
  {
    if (isValidDiskByte(value))
    {
      decodeTable[value] = count++;
    }
  }
  assert(count == 64);
  assert(decodeTable[0x96] == 0 && decodeTable[0xFF] == 63);
}

typedef struct
{
  uint32_t calls;
  uint8_t lastTrack;
  uint8_t lastSector;
  bool failing;
  bool identitySector;
} syntheticDisk;

static bool syntheticRead(void *context, uint8_t track, uint8_t sector, uint8_t *buffer)
{
  syntheticDisk *disk = context;
  disk->calls++;
  disk->lastTrack = track;
  disk->lastSector = sector;
  if (disk->failing)
  {
    return false;
  }
  for (size_t offset = 0; offset < apple2DiskSectorSize; ++offset)
  {
    buffer[offset] = disk->identitySector && track == 0 && sector == 0 ? (uint8_t)offset
                                                                         : patternByte(track, sector, offset);
  }
  return true;
}

static uint8_t decodeFourAndFour(uint8_t first, uint8_t second)
{
  return (uint8_t)(((first << 1) | 1U) & second);
}

//-- Decode one full track from the nibble stream; returns the number of sectors found (bitmask).
static unsigned decodeTrack(const uint8_t *stream, uint8_t expectedTrack, uint8_t sectors[16][256],
                            uint8_t *volume)
{
  unsigned found = 0;
  size_t length = apple2DiskNibblesPerTrack;
  for (size_t position = 0; position < length; ++position)
  {
    if (stream[position] != 0xD5 || stream[(position + 1) % length] != 0xAA ||
        stream[(position + 2) % length] != 0x96)
    {
      continue;
    }
    size_t at = position + 3;
    uint8_t headerVolume = decodeFourAndFour(stream[at % length], stream[(at + 1) % length]);
    uint8_t headerTrack = decodeFourAndFour(stream[(at + 2) % length], stream[(at + 3) % length]);
    uint8_t headerSector = decodeFourAndFour(stream[(at + 4) % length], stream[(at + 5) % length]);
    uint8_t headerChecksum = decodeFourAndFour(stream[(at + 6) % length], stream[(at + 7) % length]);
    assert(headerTrack == expectedTrack && headerSector < 16);
    assert(headerChecksum == (uint8_t)(headerVolume ^ headerTrack ^ headerSector));
    assert(stream[(at + 8) % length] == 0xDE && stream[(at + 9) % length] == 0xAA);
    *volume = headerVolume;

    assert(stream[(at + 10) % length] == 0xEB);
    size_t data = at + 11;
    while (stream[data % length] != 0xD5)
    {
      assert(stream[data % length] == 0xFF);
      ++data;
    }
    assert(stream[(data + 1) % length] == 0xAA && stream[(data + 2) % length] == 0xAD);
    uint8_t values[342];
    uint8_t previous = 0;
    for (size_t index = 0; index < 342; ++index)
    {
      int decoded = decodeTable[stream[(data + 3 + index) % length]];
      assert(decoded >= 0);
      previous = (uint8_t)(decoded ^ previous);
      values[index] = previous;
    }
    assert(decodeTable[stream[(data + 3 + 342) % length]] == previous);
    assert(stream[(data + 3 + 343) % length] == 0xDE && stream[(data + 3 + 344) % length] == 0xAA);
    for (size_t index = 0; index < 256; ++index)
    {
      uint8_t low = (uint8_t)((values[index % 86] >> (2 * (index / 86))) & 3U);
      uint8_t swapped = (uint8_t)(((low & 1U) << 1) | (low >> 1));
      sectors[headerSector][index] = (uint8_t)((values[86 + index] << 2) | swapped);
    }
    found |= 1U << headerSector;
  }
  return found;
}

static const uint8_t logicalSectorOfPhysical[16] = {0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15};

static void readTrackStream(apple2Disk *disk, uint8_t *stream)
{
  for (size_t index = 0; index < apple2DiskNibblesPerTrack; ++index)
  {
    uint8_t value = apple2DiskAccess(disk, 0xC, false, (uint64_t)index * apple2DiskCyclesPerNibble + 5, 0);
    assert((value & 0x80) != 0);
    stream[index] = value;
  }
}

static void stepToHalfTrack(apple2Disk *disk, uint8_t halfTrack)
{
  while (disk->drives[0].halfTrack < halfTrack)
  {
    uint8_t phase = (uint8_t)((disk->drives[0].halfTrack + 1) & 3);
    apple2DiskAccess(disk, (uint8_t)(phase * 2 + 1), false, 0, 0);
    apple2DiskAccess(disk, (uint8_t)(phase * 2), false, 0, 0);
  }
}

static void testControllerStream(void)
{
  buildDecodeTable();
  syntheticDisk synthetic = {.identitySector = true};
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  assert(!apple2DiskAttach(&disk, NULL, NULL));
  assert(apple2DiskAttach(&disk, syntheticRead, &synthetic));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);
  assert(disk.motorOn);

  uint8_t *stream = malloc(apple2DiskNibblesPerTrack);
  assert(stream != NULL);
  static uint8_t sectors[16][256];
  uint8_t volume = 0;
  for (uint8_t track = 0; track < apple2DiskTrackCount; track += 17)
  {
    stepToHalfTrack(&disk, (uint8_t)(track * 2));
    assert(disk.drives[0].halfTrack == track * 2);
    memset(sectors, 0, sizeof(sectors));
    readTrackStream(&disk, stream);
    assert(decodeTrack(stream, track, sectors, &volume) == 0xFFFFU);
    assert(volume == apple2DiskVolumeNumber);
    for (uint8_t physical = 0; physical < 16; ++physical)
    {
      for (size_t offset = 0; offset < 256; ++offset)
      {
        uint8_t logical = logicalSectorOfPhysical[physical];
        uint8_t expected = track == 0 && logical == 0 ? (uint8_t)offset : patternByte(track, logical, offset);
        assert(sectors[physical][offset] == expected);
      }
    }
  }
  assert(synthetic.calls > 0);

  //-- Track 34 is the last track; the head cannot go beyond the last half-track.
  stepToHalfTrack(&disk, 68);
  uint8_t previousHalfTrack = disk.drives[0].halfTrack;
  for (int count = 0; count < 12; ++count)
  {
    uint8_t phase = (uint8_t)((disk.drives[0].halfTrack + 1) & 3);
    apple2DiskAccess(&disk, (uint8_t)(phase * 2 + 1), false, 0, 0);
    apple2DiskAccess(&disk, (uint8_t)(phase * 2), false, 0, 0);
    assert(disk.drives[0].halfTrack >= previousHalfTrack);
    assert(disk.drives[0].halfTrack <= apple2DiskMaxHalfTrack);
  }
  assert(disk.drives[0].halfTrack == apple2DiskMaxHalfTrack);
  free(stream);
}

static void testControllerFailureAndHalfTrack(void)
{
  syntheticDisk synthetic = {.failing = true};
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  assert(apple2DiskAttach(&disk, syntheticRead, &synthetic));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);
  //-- A failing sector read yields only sync bytes (no address field) and is counted.
  for (size_t index = 0; index < 416; ++index)
  {
    assert((apple2DiskAccess(&disk, 0xC, false, (uint64_t)index * 32 + 5, 0) & 0xFF) == 0xFF);
  }
  assert(disk.sectorReadFailures == 1 && disk.sectorReads == 0);

  syntheticDisk good = {0};
  apple2DiskInitialize(&disk);
  assert(apple2DiskAttach(&disk, syntheticRead, &good));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);
  apple2DiskAccess(&disk, 0x3, false, 0, 0);
  assert(disk.drives[0].halfTrack == 1);
  const uint32_t before = good.calls;
  for (size_t index = 0; index < 416; ++index)
  {
    assert(apple2DiskAccess(&disk, 0xC, false, (uint64_t)index * 32 + 5, 0) == 0xFF);
  }
  assert(good.calls == before);
}

//-- Motor-off keeps the medium turning for a coast-down period; motor-on cancels it; reset stops it.
static void testMotorCoastDown(void)
{
  syntheticDisk synthetic = {0};
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  assert(apple2DiskAttach(&disk, syntheticRead, &synthetic));

  //-- Never turned on: no data.
  assert(apple2DiskAccess(&disk, 0xC, false, 1000, 0) == 0x00);

  apple2DiskAccess(&disk, 0x9, false, 5000, 0);
  assert((apple2DiskAccess(&disk, 0xC, false, 5100, 0) & 0x80) != 0);
  apple2DiskAccess(&disk, 0x8, false, 10000, 0);
  assert(!disk.motorOn);
  uint64_t offAt = 10000;
  assert(apple2DiskAccess(&disk, 0xC, false, offAt + 100, 0) != 0x00);
  assert(apple2DiskAccess(&disk, 0xC, false, offAt + apple2DiskMotorCoastCycles - 40, 0) != 0x00);
  assert(apple2DiskAccess(&disk, 0xC, false, offAt + apple2DiskMotorCoastCycles + 40, 0) == 0x00);

  //-- Motor on again within the coast period cancels it.
  apple2DiskAccess(&disk, 0x9, false, offAt + 500000, 0);
  assert(apple2DiskAccess(&disk, 0xC, false, offAt + 3 * apple2DiskMotorCoastCycles, 0) != 0x00);

  //-- Repeated motor-off writes do not extend the coast period.
  apple2DiskAccess(&disk, 0x8, false, 4000000, 0);
  apple2DiskAccess(&disk, 0x8, false, 4500000, 0);
  assert(apple2DiskAccess(&disk, 0xC, false, 4000000 + apple2DiskMotorCoastCycles + 40, 0) == 0x00);

  //-- Reset stops the medium immediately.
  apple2DiskAccess(&disk, 0x9, false, 6000000, 0);
  apple2DiskAccess(&disk, 0x8, false, 6000100, 0);
  apple2DiskResetSwitches(&disk);
  assert(apple2DiskAccess(&disk, 0xC, false, 6000200, 0) == 0x00);
}

static void testSoftSwitchesThroughCore(void)
{
  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  uint8_t rom[apple2RomSize];
  memset(rom, 0xEA, sizeof(rom));
  rom[0x2FFC] = 0x00;
  rom[0x2FFD] = 0xD0;
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);
  apple2DiskState state;
  uint8_t value;

  //-- Without media slot 6 is empty: no ROM, no softswitch side effects.
  apple2CoreGetDiskState(core, &state);
  assert(!state.attached);
  assert(apple2CoreReadMemory(core, 0xC0E9, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(!state.motorOn);
  assert(apple2CoreReadMemory(core, 0xC601, &value) == apple2CoreOk && value != 0x20);

  syntheticDisk synthetic = {0};
  assert(apple2CoreAttachDisk(NULL, syntheticRead, &synthetic) == apple2CoreInvalidArgument);
  assert(apple2CoreAttachDisk(core, NULL, &synthetic) == apple2CoreInvalidArgument);
  assert(apple2CoreAttachDisk(core, syntheticRead, &synthetic) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.attached && !state.motorOn && !state.drive2Selected && !state.q6 && !state.q7);
  assert(state.phases == 0 && state.halfTrack == 0);

  //-- Autostart signature bytes at the start of the slot 6 ROM.
  assert(apple2CoreReadMemory(core, 0xC601, &value) == apple2CoreOk && value == 0x20);
  assert(apple2CoreReadMemory(core, 0xC603, &value) == apple2CoreOk && value == 0x00);
  assert(apple2CoreReadMemory(core, 0xC605, &value) == apple2CoreOk && value == 0x03);
  assert(apple2CoreReadMemory(core, 0xC607, &value) == apple2CoreOk && value == 0x3C);

  assert(apple2CoreReadMemory(core, 0xC0E9, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.motorOn);
  assert(apple2CoreReadMemory(core, 0xC0E8, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(!state.motorOn);

  assert(apple2CoreReadMemory(core, 0xC0EB, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.drive2Selected);
  assert(apple2CoreReadMemory(core, 0xC0EA, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(!state.drive2Selected);

  //-- Phase 1 then 2 energized moves the head one full track (two half-tracks).
  assert(apple2CoreReadMemory(core, 0xC0E3, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.halfTrack == 1 && state.phases == 0x02);
  assert(apple2CoreReadMemory(core, 0xC0E2, &value) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0xC0E5, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.halfTrack == 2 && state.phases == 0x04);
  assert(apple2CoreReadMemory(core, 0xC0E4, &value) == apple2CoreOk);
  //-- And back down again.
  assert(apple2CoreReadMemory(core, 0xC0E3, &value) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0xC0E2, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.halfTrack == 1);
  //-- Re-energizing a phase that is already on does not move the head; at track 0 the head stays put.
  assert(apple2CoreReadMemory(core, 0xC0E3, &value) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0xC0E3, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.halfTrack == 1);
  assert(apple2CoreReadMemory(core, 0xC0E2, &value) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0xC0E1, &value) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0xC0E0, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.halfTrack == 0);
  for (int count = 0; count < 4; ++count)
  {
    assert(apple2CoreReadMemory(core, 0xC0E7, &value) == apple2CoreOk);
    assert(apple2CoreReadMemory(core, 0xC0E6, &value) == apple2CoreOk);
    assert(apple2CoreReadMemory(core, 0xC0E1, &value) == apple2CoreOk);
    assert(apple2CoreReadMemory(core, 0xC0E0, &value) == apple2CoreOk);
  }
  apple2CoreGetDiskState(core, &state);
  assert(state.halfTrack == 0);
  for (int phase = 0; phase < 4; ++phase)
  {
    assert(apple2CoreReadMemory(core, (uint16_t)(0xC0E0 + phase * 2), &value) == apple2CoreOk);
  }
  apple2CoreGetDiskState(core, &state);
  assert(state.phases == 0);

  //-- Q6/Q7 selection and write-protect sense (read-only media always report protected).
  assert(apple2CoreReadMemory(core, 0xC0ED, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.q6 && !state.q7);
  assert(apple2CoreReadMemory(core, 0xC0EE, &value) == apple2CoreOk && (value & 0x80) != 0);
  apple2CoreGetDiskState(core, &state);
  assert(state.q6 && !state.q7);
  assert(apple2CoreReadMemory(core, 0xC0EC, &value) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(!state.q6 && !state.q7);

  //-- Selecting write-latch mode on a read-only image does not commit any data.
  const uint32_t readsBefore = synthetic.calls;
  assert(apple2CoreWriteMemory(core, 0xC0ED, 0xFF) == apple2CoreOk);
  assert(apple2CoreWriteMemory(core, 0xC0EF, 0xFF) == apple2CoreOk);
  assert(apple2CoreWriteMemory(core, 0xC0ED, 0xAA) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.q6 && state.q7 && state.writeAttempts == 0 && state.writeFailures == 0);
  assert(apple2CoreWriteMemory(core, 0xC600, 0x55) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0xC600, &value) == apple2CoreOk && value != 0x55);
  assert(synthetic.calls == readsBefore);

  assert(apple2CoreReset(core) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.attached && !state.motorOn && !state.q6 && !state.q7 && state.phases == 0);

  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(!state.attached && !state.motorOn);
  assert(apple2CoreReadMemory(core, 0xC601, &value) == apple2CoreOk && value != 0x20);
  assert(apple2CoreDetachDisk(NULL) == apple2CoreInvalidArgument);
  apple2CoreDestroy(core);
}

static void readRom(const char *path, uint8_t *rom)
{
  FILE *file = fopen(path, "rb");
  assert(file != NULL);
  assert(fread(rom, 1, apple2RomSize, file) == apple2RomSize);
  assert(fgetc(file) == EOF);
  assert(fclose(file) == 0);
}

//-- The guest (Autostart ROM + project-authored slot-6 boot ROM) must itself read the boot sector.
static void testGuestBootSector(void)
{
  uint8_t *image = malloc(imageBytes);
  assert(image != NULL);
  fillPatternImage(image);
  static const uint8_t bootCode[] = {0x01, 0xA9, 0x42, 0x8D, 0x00, 0x03, 0xA9, 0x77, 0x8D, 0x01, 0x03,
                                     0x4C, 0x0B, 0x08};
  memcpy(image, bootCode, sizeof(bootCode));
  char path[64];
  makeTempPath(path, sizeof(path));
  writeFile(path, image, imageBytes);

  apple2DiskImage disk;
  apple2DiskImageInitialize(&disk);
  assert(apple2DiskImageOpen(&disk, path) == apple2DiskImageOk);
  uint8_t rom[apple2RomSize];
  readRom(APPLE2_SYSTEM_ROM_PATH, rom);
  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreSetCharacterOptions(core, true, true) == apple2CoreOk);
  assert(apple2CoreAttachDisk(core, apple2DiskImageReadSectorCallback, &disk) == apple2CoreOk);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);
  assert(apple2CoreRunCycles(core, 2000000) == apple2CoreOk);

  uint8_t value;
  assert(apple2CoreReadMemory(core, 0x0300, &value) == apple2CoreOk && value == 0x42);
  assert(apple2CoreReadMemory(core, 0x0301, &value) == apple2CoreOk && value == 0x77);
  for (size_t offset = 0; offset < 256; ++offset)
  {
    assert(apple2CoreReadMemory(core, (uint16_t)(0x0800 + offset), &value) == apple2CoreOk);
    assert(value == image[offset]);
  }
  apple2DiskState state;
  apple2CoreGetDiskState(core, &state);
  assert(state.attached && state.motorOn && !state.drive2Selected);
  assert(state.halfTrack == 0 && state.sectorReads >= 1 && state.sectorReadFailures == 0);
  assert(state.writeAttempts == 0);

  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreDestroy(core);
  assert(apple2DiskImageClose(&disk) == apple2DiskImageOk);
  size_t afterSize;
  uint8_t *after = readWholeFile(path, &afterSize);
  assert(afterSize == imageBytes && memcmp(after, image, imageBytes) == 0);
  free(after);
  free(image);
  assert(unlink(path) == 0);
}

static void typeText(apple2Core *core, const char *text)
{
  for (size_t index = 0; text[index] != '\0'; ++index)
  {
    assert(apple2CorePressKey(core, (uint8_t)text[index]) == apple2CoreOk);
    for (size_t attempt = 0; attempt < 400 && apple2CoreKeyPending(core); ++attempt)
    {
      assert(apple2CoreRunCycles(core, 5000) == apple2CoreOk);
    }
    assert(!apple2CoreKeyPending(core));
  }
}

//-- Reads one row of whichever screen is active: the 80-column card when selected, else the 40-column page.
static size_t readActiveRow(apple2Core *core, size_t row, char *line, size_t lineSize)
{
  apple2VideoState video;
  apple2CoreGetVideoState(core, &video);
  size_t columns = video.videxTextMode ? apple2VidexTextColumns : apple2TextColumns;
  assert(lineSize > columns);
  for (size_t column = 0; column < columns; ++column)
  {
    uint8_t value;
    if (video.videxTextMode)
    {
      assert(apple2CoreReadVidexTextCell(core, row, column, &value) == apple2CoreOk);
      line[column] = (char)(value & 0x7F);
    }
    else
    {
      assert(apple2CoreReadTextCell(core, video.page2, row, column, &value) == apple2CoreOk);
      line[column] = apple2CoreDecodeTextCharacter(core, value);
    }
  }
  line[columns] = '\0';
  return columns;
}

static size_t activeRowCount(apple2Core *core)
{
  apple2VideoState video;
  apple2CoreGetVideoState(core, &video);
  return video.videxTextMode ? apple2VidexTextRows : apple2TextRows;
}

static bool isVidexActive(apple2Core *core)
{
  apple2VideoState video;
  apple2CoreGetVideoState(core, &video);
  return video.videxTextMode;
}

static bool screenContains(apple2Core *core, const char *text)
{
  for (size_t row = 0; row < activeRowCount(core); ++row)
  {
    char line[apple2VidexTextColumns + 1];
    readActiveRow(core, row, line, sizeof(line));
    if (strstr(line, text) != NULL)
    {
      return true;
    }
  }
  return false;
}

static void dumpScreen(apple2Core *core)
{
  for (size_t row = 0; row < activeRowCount(core); ++row)
  {
    char line[apple2VidexTextColumns + 1];
    readActiveRow(core, row, line, sizeof(line));
    printf("|%s|\n", line);
  }
}

//-- Every sector of the real DOS 3.3 image, delivered through the controller stream, equals the image bytes.
static void testStreamMatchesDosImage(const char *imagePath)
{
  buildDecodeTable();
  apple2DiskImage image;
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpen(&image, imagePath) == apple2DiskImageOk);
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  assert(apple2DiskAttach(&disk, apple2DiskImageReadSectorCallback, &image));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);
  uint8_t *stream = malloc(apple2DiskNibblesPerTrack);
  assert(stream != NULL);
  static uint8_t sectors[16][256];
  for (uint8_t track = 0; track < apple2DiskTrackCount; ++track)
  {
    stepToHalfTrack(&disk, (uint8_t)(track * 2));
    readTrackStream(&disk, stream);
    uint8_t volume;
    assert(decodeTrack(stream, track, sectors, &volume) == 0xFFFFU);
    for (uint8_t physical = 0; physical < 16; ++physical)
    {
      uint8_t expected[256];
      assert(apple2DiskImageReadSector(&image, track, logicalSectorOfPhysical[physical], expected) ==
             apple2DiskImageOk);
      assert(memcmp(sectors[physical], expected, sizeof(expected)) == 0);
    }
  }
  free(stream);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
}

static void testDosBoot(const char *imagePath, bool runProgram)
{
  size_t beforeSize;
  uint8_t *before = readWholeFile(imagePath, &beforeSize);
  apple2DiskImage disk;
  apple2DiskImageInitialize(&disk);
  assert(apple2DiskImageOpen(&disk, imagePath) == apple2DiskImageOk);
  uint8_t rom[apple2RomSize];
  readRom(APPLE2_SYSTEM_ROM_PATH, rom);
  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreSetCharacterOptions(core, true, true) == apple2CoreOk);
  //-- The machine starts with the 80-column card selected; nothing (HELLO included) has to run PR#3.
  assert(apple2CoreSetBootIn80Columns(core, runProgram) == apple2CoreOk);
  assert(apple2CoreAttachDisk(core, apple2DiskImageReadSectorCallback, &disk) == apple2CoreOk);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);

  bool booted = false;
  for (int step = 0; step < 60 && !booted; ++step)
  {
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    booted = isVidexActive(core) && screenContains(core, "]");
  }
  apple2DiskState state;
  apple2CoreGetDiskState(core, &state);
  printf("boot: booted=%d halfTrack=%u sectorReads=%u failures=%u writeAttempts=%u\n", booted, state.halfTrack,
         state.sectorReads, state.sectorReadFailures, state.writeAttempts);
  if (!booted)
  {
    dumpScreen(core);
    exit(2);
  }
  assert(state.writeAttempts == 0 && state.sectorReadFailures == 0);

  if (runProgram)
  {
    //-- DOS booted with the 80-column card already selected and no error was reported.
    assert(isVidexActive(core));
    assert(!screenContains(core, "NOT FOUND"));
    typeText(core, "HOME\r");
    assert(apple2CoreRunCycles(core, 200000) == apple2CoreOk);
    assert(isVidexActive(core));
    uint32_t readsBeforeLoad = state.sectorReads;
    typeText(core, "LOAD TEST-NONGR\r");
    assert(apple2CoreRunCycles(core, 6000000) == apple2CoreOk);
    assert(!screenContains(core, "ERROR"));
    assert(!screenContains(core, "NOT FOUND"));
    apple2CoreGetDiskState(core, &state);
    assert(state.sectorReads > readsBeforeLoad && state.sectorReadFailures == 0);
    uint8_t programEndLow;
    uint8_t programEndHigh;
    assert(apple2CoreReadMemory(core, 0x00AF, &programEndLow) == apple2CoreOk);
    assert(apple2CoreReadMemory(core, 0x00B0, &programEndHigh) == apple2CoreOk);
    uint16_t programEnd = (uint16_t)(programEndLow | (programEndHigh << 8));
    printf("load: programEnd=$%04X (program bytes=%u)\n", programEnd, programEnd - 0x0801U);
    assert(programEnd > 0x0801);

    //-- CATALOG and LOAD only read: no write attempt may reach the Disk II (it would light the red LED).
    typeText(core, "CATALOG\r");
    assert(apple2CoreRunCycles(core, 3000000) == apple2CoreOk);
    assert(screenContains(core, "TEST-NONGR"));
    apple2CoreGetDiskState(core, &state);
    assert(state.writeAttempts == 0);

    typeText(core, "RUN\r");
    for (int step = 0; step < 40 && !screenContains(core, "ALL SYSTEM TESTS OK"); ++step)
    {
      assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    }
    printf("screen after RUN:\n");
    dumpScreen(core);
    assert(screenContains(core, "ALL SYSTEM TESTS OK"));
    assert(isVidexActive(core));

    //-- PR#0 returns to the 40-column screen and clears it; PR#3 comes back with a cleared 80-column screen.
    typeText(core, "PR#0\r");
    assert(apple2CoreRunCycles(core, 400000) == apple2CoreOk);
    assert(!isVidexActive(core));
    typeText(core, "PRINT \"BACK40\"\r");
    assert(apple2CoreRunCycles(core, 400000) == apple2CoreOk);
    assert(screenContains(core, "BACK40"));
    typeText(core, "PR#3\r");
    assert(apple2CoreRunCycles(core, 400000) == apple2CoreOk);
    assert(isVidexActive(core));
    typeText(core, "HOME\r");
    assert(apple2CoreRunCycles(core, 400000) == apple2CoreOk);
    typeText(core, "VTAB 5: HTAB 10: PRINT \"POSX\"\r");
    assert(apple2CoreRunCycles(core, 400000) == apple2CoreOk);
    char positionedRow[apple2VidexTextColumns + 1];
    readActiveRow(core, 4, positionedRow, sizeof(positionedRow));
    assert(strncmp(positionedRow + 9, "POSX", 4) == 0);
  }
  apple2CoreGetDiskState(core, &state);
  assert(state.writeAttempts == 0);
  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreDestroy(core);
  assert(apple2DiskImageClose(&disk) == apple2DiskImageOk);
  size_t afterSize;
  uint8_t *after = readWholeFile(imagePath, &afterSize);
  assert(afterSize == beforeSize && memcmp(after, before, beforeSize) == 0);
  free(after);
  free(before);
}

//— The ProDOS tests write to their data volume, so they work on a scratch copy that is removed afterwards.
static void makeScratchCopy(const char *source, char *scratch, size_t scratchSize)
{
  snprintf(scratch, scratchSize, "%s.run", source);
  size_t size;
  uint8_t *data = readWholeFile(source, &size);
  FILE *file = fopen(scratch, "wb");
  assert(file != NULL);
  assert(fwrite(data, 1, size, file) == size);
  assert(fclose(file) == 0);
  free(data);
}

static bool proDosDirectoryHasFile(apple2DiskImage *image, const char *fileName)
{
  uint8_t block[apple2SmartPortBlockSize];
  for (uint16_t directoryBlock = 2; directoryBlock < 6; ++directoryBlock)
  {
    if (apple2DiskImageReadBlock(image, directoryBlock, block) != apple2DiskImageOk)
    {
      return false;
    }
    //— The first directory block starts with the volume header entry; every later block has 13 file entries.
    for (uint8_t entry = directoryBlock == 2 ? 1 : 0; entry < 13; ++entry)
    {
      size_t offset = 4U + (size_t)entry * 0x27U;
      uint8_t nameLength = block[offset] & 0x0FU;
      if ((block[offset] & 0xF0U) == 0 || nameLength != strlen(fileName) ||
          memcmp(&block[offset + 1], fileName, nameLength) != 0)
      {
        continue;
      }
      return true;
    }
  }
  return false;
}

static void proDosSelectBasicAndSave(apple2Core *core, apple2DiskImage *dataDisk)
{
  //— The ProDOS 2.4.2 disk starts the Bitsy Bye selector; move the cursor down to BASIC.SYSTEM and start it.
  assert(apple2CoreRunCycles(core, 5000000) == apple2CoreOk);
  //— A volume whose first system file is BASIC.SYSTEM already shows the BASIC prompt; otherwise use the selector.
  if (!screenContains(core, "PRODOS BASIC") && !screenContains(core, "]"))
  {
    typeText(core, "\x0A\x0A\x0A");
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    typeText(core, "\r");
    assert(apple2CoreRunCycles(core, 5000000) == apple2CoreOk);
  }
  assert(screenContains(core, "PRODOS BASIC") || screenContains(core, "]"));
  typeText(core, "CATALOG /DATA800\r");
  assert(apple2CoreRunCycles(core, 3000000) == apple2CoreOk);
  if (!screenContains(core, "DATA800"))
  {
    dumpScreen(core);
  }
  assert(screenContains(core, "DATA800"));
  typeText(core, "PREFIX /DATA800\r");
  assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
  typeText(core, "10 PRINT \"SMARTPORT\"\r");
  assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
  typeText(core, "SAVE TEST\r");
  assert(apple2CoreRunCycles(core, 3000000) == apple2CoreOk);
  if (!proDosDirectoryHasFile(dataDisk, "TEST"))
  {
    dumpScreen(core);
  }
  assert(proDosDirectoryHasFile(dataDisk, "TEST"));
}

static void testProDosSmartPortBoot(const char *bootImagePath, const char *dataImagePath)
{
  char scratchPath[512];
  makeScratchCopy(dataImagePath, scratchPath, sizeof(scratchPath));
  apple2DiskImage bootDisk;
  apple2DiskImageInitialize(&bootDisk);
  assert(apple2DiskImageOpenProfileMode(&bootDisk, bootImagePath, apple2DiskImageProfile140k,
                                        apple2DiskImageOrderAuto, true) == apple2DiskImageOk);
  apple2DiskImage dataDisk;
  apple2DiskImageInitialize(&dataDisk);
  assert(apple2DiskImageOpenProfileMode(&dataDisk, scratchPath, apple2DiskImageProfile800k,
                                        apple2DiskImageOrderAuto, false) == apple2DiskImageOk);
  assert(dataDisk.probe.content == apple2DiskImageContentProDos);

  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 1, bootDisk.trackCount * 8U, apple2DiskImageReadBlockCallback,
                                         NULL, &bootDisk) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 2, dataDisk.trackCount * 8U, apple2DiskImageReadBlockCallback,
                                         apple2DiskImageWriteBlockCallback, &dataDisk) == apple2CoreOk);
  uint8_t rom[apple2RomSize];
  readRom(APPLE2_SYSTEM_ROM_PATH, rom);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);

  //— No Disk II is attached, so the autostart ROM enters Applesoft BASIC; boot the SmartPort slot from there.
  assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
  typeText(core, "PR#5\r");
  bool booted = false;
  for (int step = 0; step < 60 && !booted; ++step)
  {
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    booted = screenContains(core, "PRODOS");
  }
  if (!booted)
  {
    fprintf(stderr, "ProDOS boot failed: order=%d content=%d\n", (int)bootDisk.order,
            (int)bootDisk.probe.content);
    dumpScreen(core);
  }
  assert(booted);
  proDosSelectBasicAndSave(core, &dataDisk);
  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreDestroy(core);
  assert(apple2DiskImageClose(&dataDisk) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&bootDisk) == apple2DiskImageOk);
  assert(remove(scratchPath) == 0);
}

static void testProDosAutostartBoot(const char *bootImagePath, const char *dataImagePath)
{
  char scratchPath[512];
  makeScratchCopy(dataImagePath, scratchPath, sizeof(scratchPath));
  apple2DiskImage bootDisk;
  apple2DiskImageInitialize(&bootDisk);
  assert(apple2DiskImageOpenProfileMode(&bootDisk, bootImagePath, apple2DiskImageProfile140k,
                                        apple2DiskImageOrderAuto, true) == apple2DiskImageOk);
  apple2DiskImage dataDisk;
  apple2DiskImageInitialize(&dataDisk);
  assert(apple2DiskImageOpenProfileMode(&dataDisk, scratchPath, apple2DiskImageProfile800k,
                                        apple2DiskImageOrderAuto, false) == apple2DiskImageOk);

  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  //— PR6.1 holds the ProDOS boot volume as an autostart block device, PR6.2 the 800K data volume.
  assert(apple2CoreAttachBlockDevice(core, 6, 1, bootDisk.trackCount * 8U, true, apple2DiskImageReadBlockCallback,
                                     NULL, &bootDisk) == apple2CoreOk);
  assert(apple2CoreAttachBlockDevice(core, 6, 2, dataDisk.trackCount * 8U, true, apple2DiskImageReadBlockCallback,
                                     apple2DiskImageWriteBlockCallback, &dataDisk) == apple2CoreOk);
  assert(apple2CoreAttachDiskDrive(core, 6, 0, apple2DiskSectorOrderDos, apple2DiskTrackCount, NULL, NULL) ==
         apple2CoreInvalidArgument);
  uint8_t rom[apple2RomSize];
  readRom(APPLE2_SYSTEM_ROM_PATH, rom);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);

  //— No keyboard input: the Autostart ROM must find the slot-6 boot signature and start ProDOS by itself.
  bool booted = false;
  for (int step = 0; step < 60 && !booted; ++step)
  {
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    booted = screenContains(core, "PRODOS") || screenContains(core, "HELLO, APPLE II");
  }
  if (!booted)
  {
    dumpScreen(core);
  }
  assert(booted);
  proDosSelectBasicAndSave(core, &dataDisk);
  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreDestroy(core);
  assert(apple2DiskImageClose(&dataDisk) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&bootDisk) == apple2DiskImageOk);
  assert(remove(scratchPath) == 0);
}

int main(int argc, char **argv)
{
  if (argc == 4 && strcmp(argv[1], "--prodos-smartport-boot") == 0)
  {
    testProDosSmartPortBoot(argv[2], argv[3]);
    puts("PASS: ProDOS boot, SmartPort catalog and BASIC SAVE to the 800K volume");
    return 0;
  }
  if (argc == 4 && strcmp(argv[1], "--prodos-autostart-boot") == 0)
  {
    testProDosAutostartBoot(argv[2], argv[3]);
    puts("PASS: ProDOS autostart from PR6.1, catalog and BASIC SAVE to the PR6.2 800K volume");
    return 0;
  }
  if (argc >= 2 && strcmp(argv[1], "--dos-boot") == 0)
  {
    testDosBoot(argc >= 3 ? argv[2] : APPLE2_SYSTEM_DISK_PATH, true);
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--dos-stream") == 0)
  {
    testStreamMatchesDosImage(APPLE2_SYSTEM_DISK_PATH);
    testStreamMatchesDosImage(APPLE2_BASE_DISK_PATH);
    puts("PASS: Disk II nibble stream equals every sector of the DOS 3.3 images");
    return 0;
  }
  testImageOpenAndBounds();
  testWritableDiskImage();
  testControllerStream();
  testControllerFailureAndHalfTrack();
  testMotorCoastDown();
  testSoftSwitchesThroughCore();
  testSmartPortBlockCall();
  testGuestBootSector();
  puts("PASS: Disk II image, SmartPort block calls, controller stream, softswitches and guest boot-sector read");
  return 0;
}
