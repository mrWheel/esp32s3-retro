#include "apple2Core.h"
#include "apple2Disk.h"
#include "apple2DiskImage.h"
#include "hostExchange.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//-- Disk II (slot 6, drive 1, 16-sector) host tests, including writable media.
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
  for (uint8_t unit = 1; unit <= 4; ++unit)
  {
    assert(apple2CoreAttachSmartPortDevice(core, unit, 2, smartPortRead, NULL, &fixture) == apple2CoreOk);
  }
  assert(apple2CoreAttachSmartPortDevice(core, 5, 2, smartPortRead, NULL, &fixture) ==
         apple2CoreInvalidArgument);
  assert(apple2CoreReadMemory(core, 0xC5FE, &signature) == apple2CoreOk && signature == 0xF3);
  for (uint8_t unit = 1; unit <= 4; ++unit)
  {
    assert(apple2CoreDetachSmartPortDevice(core, unit) == apple2CoreOk);
  }
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

static void testWritableBlockImage800k(void)
{
  char path[64];
  makeTempPath(path, sizeof(path));
  uint8_t *blank = calloc(apple2DiskImage800kSize, 1);
  assert(blank != NULL);
  writeFile(path, blank, apple2DiskImage800kSize);
  free(blank);

  apple2DiskImage image;
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfileMode(&image, path, apple2DiskImageProfile800k,
                                        apple2DiskImageOrderProdos, false) == apple2DiskImageOk);
  uint8_t expected[apple2SmartPortBlockSize];
  uint8_t actual[apple2SmartPortBlockSize];
  for (size_t index = 0; index < sizeof(expected); ++index)
  {
    expected[index] = (uint8_t)(index * 17U + 3U);
  }
  assert(apple2DiskImageWriteBlock(&image, 1599, expected) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);

  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, path, apple2DiskImageProfile800k,
                                    apple2DiskImageOrderProdos) == apple2DiskImageOk);
  assert(apple2DiskImageReadBlock(&image, 1599, actual) == apple2DiskImageOk);
  assert(memcmp(actual, expected, sizeof(expected)) == 0);
  assert(apple2DiskImageReadBlock(&image, 1600, actual) == apple2DiskImageOutOfRange);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
  assert(remove(path) == 0);
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

//-- The guest boot ROM must read the boot sector and advance the next-sector request after each read.
static void testGuestBootSector(void)
{
  uint8_t *image = malloc(imageBytes);
  assert(image != NULL);
  fillPatternImage(image);
  static const uint8_t bootCode[] = {
      0x01,                   //-- Boot signature.
      0xAD, 0x00, 0x03,       //-- LDA $0300
      0xD0, 0x18,             //-- BNE done
      0xA9, 0x42, 0x8D, 0x00, 0x03, //-- STA $0300
      0xA9, 0x77, 0x8D, 0x01, 0x03, //-- Preserve the original boot-sector assertions.
      0xA9, 0x02, 0x85, 0x3D, //-- Request physical sector 2.
      0xA9, 0x00, 0x85, 0x26, //-- Buffer pointer = $0900.
      0xA9, 0x09, 0x85, 0x27,
      0x20, 0x5C, 0xC6,       //-- JSR slot-6 read entry.
      0x4C, 0x1F, 0x08};      //-- done: JMP $081F
  memcpy(image, bootCode, sizeof(bootCode));
  char path[64];
  makeTempPath(path, sizeof(path));
  writeFile(path, image, imageBytes);

  apple2DiskImage disk;
  apple2DiskImageInitialize(&disk);
  assert(apple2DiskImageOpen(&disk, path) == apple2DiskImageOk);
  uint8_t expectedSecondSector[apple2DiskImageSectorSize];
  assert(apple2DiskImageReadSector(&disk, 0, 14, expectedSecondSector) == apple2DiskImageOk);
  uint8_t rom[apple2RomSize];
  readRom(APPLE2_SYSTEM_ROM_PATH, rom);
  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreSetCharacterOptions(core, true, true) == apple2CoreOk);
  assert(apple2CoreAttachDisk(core, apple2DiskImageReadSectorCallback, &disk) == apple2CoreOk);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);
  assert(apple2CoreRunCycles(core, 4000000) == apple2CoreOk);

  uint8_t value;
  assert(apple2CoreReadMemory(core, 0x0300, &value) == apple2CoreOk && value == 0x42);
  assert(apple2CoreReadMemory(core, 0x0301, &value) == apple2CoreOk && value == 0x77);
  assert(apple2CoreReadMemory(core, 0x3D, &value) == apple2CoreOk && value == 0x03);
  assert(apple2CoreReadMemory(core, 0x27, &value) == apple2CoreOk && value == 0x0A);
  for (size_t offset = 0; offset < sizeof(expectedSecondSector); ++offset)
  {
    assert(apple2CoreReadMemory(core, (uint16_t)(0x0900 + offset), &value) == apple2CoreOk);
    assert(value == expectedSecondSector[offset]);
  }
  for (size_t offset = 0; offset < 256; ++offset)
  {
    assert(apple2CoreReadMemory(core, (uint16_t)(0x0800 + offset), &value) == apple2CoreOk);
    assert(value == image[offset]);
  }
  //-- Guest boot code decodes sectors with the translate table the slot ROM leaves behind: table[$02D6 + diskByte]
  //-- holds the six-bit value of every valid disk byte (Apple Pascal 1 boot reads directory blocks that way).
  for (size_t sixBit = 0; sixBit < sizeof(testGcrWriteTable); ++sixBit)
  {
    assert(apple2CoreReadMemory(core, (uint16_t)(0x02D6 + testGcrWriteTable[sixBit]), &value) == apple2CoreOk);
    assert(value == sixBit);
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
  size_t columns = video.videxTextMode || video.col80 ? apple2VidexTextColumns : apple2TextColumns;
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

//-- Every sector of a real image, delivered through the controller stream, equals the image bytes.
static void testStreamMatchesImage(const char *imagePath)
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

static void testProDosAutostartBoot(const char *bootImagePath, const char *dataImagePath, bool bootIn80Columns)
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
  //— SD6.1 holds the ProDOS boot volume as an autostart block device, SD6.2 the 800K data volume.
  assert(apple2CoreAttachBlockDevice(core, 6, 1, bootDisk.trackCount * 8U, true, apple2DiskImageReadBlockCallback,
                                     NULL, &bootDisk) == apple2CoreOk);
  assert(apple2CoreAttachBlockDevice(core, 6, 2, dataDisk.trackCount * 8U, true, apple2DiskImageReadBlockCallback,
                                     apple2DiskImageWriteBlockCallback, &dataDisk) == apple2CoreOk);
  assert(apple2CoreAttachDiskDrive(core, 6, 0, apple2DiskSectorOrderDos, apple2DiskTrackCount, NULL, NULL) ==
         apple2CoreInvalidArgument);
  uint8_t rom[apple2RomSize];
  readRom(APPLE2_SYSTEM_ROM_PATH, rom);
  assert(apple2CoreSetBootIn80Columns(core, bootIn80Columns) == apple2CoreOk);
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
  if (bootIn80Columns)
  {
    //— ProDOS BASIC.SYSTEM resets CSW to COUT1 and swaps its own hook on every key read; the card must stay selected.
    assert(apple2CoreRunCycles(core, 3000000) == apple2CoreOk);
    assert(activeRowCount(core) == apple2VidexTextRows);
    assert(screenContains(core, "HELLO, APPLE II! with 80 Columns"));
    typeText(core, "PRINT 1234\r");
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    assert(activeRowCount(core) == apple2VidexTextRows);
    assert(screenContains(core, "]PRINT 1234"));
    //— PR#3 under BASIC.SYSTEM re-selects the card and the echo of typed characters follows the cursor.
    typeText(core, "PR#3\r");
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    typeText(core, "PRINT 5678\r");
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    assert(activeRowCount(core) == apple2VidexTextRows);
    assert(screenContains(core, "]PRINT 5678"));
    //— PR#0 is an explicit request for the 40-column screen and must be honoured.
    typeText(core, "PR#0\r");
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    typeText(core, "PRINT 9012\r");
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    assert(activeRowCount(core) == apple2TextRows);
    assert(screenContains(core, "]PRINT 9012"));
    typeText(core, "PR#3\r");
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    assert(activeRowCount(core) == apple2VidexTextRows);
  }
  proDosSelectBasicAndSave(core, &dataDisk);
  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreDestroy(core);
  assert(apple2DiskImageClose(&dataDisk) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&bootDisk) == apple2DiskImageOk);
  assert(remove(scratchPath) == 0);
}

//-- Apple Pascal 1 boots from slot 6, drive 1 through the project slot ROM and reaches its command line.
static void testPascalBoot(const char *bootImagePath)
{
  apple2DiskImage bootDisk;
  apple2DiskImageInitialize(&bootDisk);
  assert(apple2DiskImageOpenProfileMode(&bootDisk, bootImagePath, apple2DiskImageProfile140k,
                                        apple2DiskImageOrderAuto, true) == apple2DiskImageOk);
  assert(bootDisk.probe.content == apple2DiskImageContentPascal);
  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreSetCharacterOptions(core, true, true) == apple2CoreOk);
  assert(apple2CoreSetBootIn80Columns(core, true) == apple2CoreOk);
  apple2DiskSectorOrder order = bootDisk.order == apple2DiskImageOrderProdos ? apple2DiskSectorOrderProdos
                                                                             : apple2DiskSectorOrderDos;
  assert(apple2CoreAttachDiskDrive(core, 6, 0, order, bootDisk.trackCount, apple2DiskImageReadSectorCallback,
                                   &bootDisk) == apple2CoreOk);
  uint8_t rom[apple2RomSize];
  readRom(APPLE2_SYSTEM_ROM_PATH, rom);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);

  bool booted = false;
  for (int step = 0; step < 60 && !booted; ++step)
  {
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    booted = screenContains(core, "Command: E(dit, R(un, F(ile, C(omp, L(ink, X(ecute, A(ssem, D(ebug,? [1.1]");
  }
  if (!booted)
  {
    dumpScreen(core);
  }
  assert(booted);
  //-- Pascal found the slot-3 card through the Pascal 1.1 firmware protocol and uses it as its 80x24 console.
  apple2VideoState video;
  apple2CoreGetVideoState(core, &video);
  assert(video.videxTextMode);
  assert(screenContains(core, "Welcome APPLE1, to Apple II Pascal 1.1"));
  assert(!screenContains(core, "No file SYSTEM.APPLE"));
  uint8_t slotByte;
  assert(apple2CoreReadMemory(core, 0xC305, &slotByte) == apple2CoreOk && slotByte == 0x38);
  assert(apple2CoreReadMemory(core, 0xC307, &slotByte) == apple2CoreOk && slotByte == 0x18);
  assert(apple2CoreReadMemory(core, 0xC30B, &slotByte) == apple2CoreOk && slotByte == 0x01);
  assert(apple2CoreReadMemory(core, 0xBF2A, &slotByte) == apple2CoreOk && slotByte == 0x06);
  char line[apple2VidexTextColumns + 1];
  readActiveRow(core, 0, line, sizeof(line));
  assert(strlen(line) == apple2VidexTextColumns && strncmp(line, "Command:", 8) == 0);

  //-- A key goes through the card: '?' asks Pascal for the next command line, drawn with GOTOXY and clear-to-end-of-line.
  typeText(core, "?");
  assert(apple2CoreRunCycles(core, 500000) == apple2CoreOk);
  if (!screenContains(core, "Command: U(ser restart, I(nitialize, H(alt, S(wap, M(ake exec"))
  {
    dumpScreen(core);
  }
  assert(screenContains(core, "Command: U(ser restart, I(nitialize, H(alt, S(wap, M(ake exec"));
  assert(!screenContains(core, "E(dit, R(un"));
  readActiveRow(core, 0, line, sizeof(line));
  assert(line[70] == ' ' && line[79] == ' ');
  apple2DiskState state;
  apple2CoreGetDiskState(core, &state);
  assert(state.sectorReadFailures == 0 && state.writeAttempts == 0);

  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreDestroy(core);
  assert(apple2DiskImageClose(&bootDisk) == apple2DiskImageOk);
}

//-- The Apple IIe 128K Pascal system (128K.APPLE/128K.PASCAL on the boot disk) needs auxiliary memory.
static void testPascalIIeBoot(const char *bootImagePath, const char *iieRomPath, apple2DiskImageProfile profile,
                              bool blockBoot)
{
  apple2DiskImage bootDisk;
  apple2DiskImageInitialize(&bootDisk);
  assert(apple2DiskImageOpenProfileMode(&bootDisk, bootImagePath, profile, apple2DiskImageOrderAuto, true) ==
         apple2DiskImageOk);
  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreEnableIIe(core) == apple2CoreOk);
  assert(apple2CoreSetCharacterOptions(core, true, true) == apple2CoreOk);
  if (blockBoot)
  {
    assert(bootDisk.probe.content == apple2DiskImageContentPascal);
    assert(apple2CoreAttachBlockDevice(core, 6, 1, (uint32_t)bootDisk.trackCount * 8U, true,
                                       apple2DiskImageReadBlockCallback, NULL, &bootDisk) == apple2CoreOk);
  }
  else
  {
    apple2DiskSectorOrder order = bootDisk.order == apple2DiskImageOrderProdos ? apple2DiskSectorOrderProdos
                                                                               : apple2DiskSectorOrderDos;
    assert(apple2CoreAttachDiskDrive(core, 6, 0, order, bootDisk.trackCount, apple2DiskImageReadSectorCallback,
                                     &bootDisk) == apple2CoreOk);
  }
  uint8_t rom[apple2IIeRomSize];
  FILE *file = fopen(iieRomPath, "rb");
  assert(file != NULL && fread(rom, 1, sizeof(rom), file) == sizeof(rom));
  fclose(file);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);

  bool booted = false;
  for (int step = 0; step < 120 && !booted; ++step)
  {
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    booted = screenContains(core, "Command:");
  }
  dumpScreen(core);
  assert(booted);
  assert(screenContains(core, "Pascal 1.3"));
  assert(screenContains(core, profile == apple2DiskImageProfile800k ? "Pascal system size is 64K"
                                                                      : "Pascal system size is 128K"));
  if (profile == apple2DiskImageProfile140k)
  {
    assert(!screenContains(core, "128K memory required"));
  }
  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreDestroy(core);
  assert(apple2DiskImageClose(&bootDisk) == apple2DiskImageOk);
}

static bool pascalVolumeHasFile(apple2DiskImage *image, const char *fileName)
{
  uint8_t header[apple2SmartPortBlockSize];
  if (apple2DiskImageReadBlock(image, 2, header) != apple2DiskImageOk)
  {
    return false;
  }

  uint16_t directoryEnd = (uint16_t)(header[2] | ((uint16_t)header[3] << 8));
  if (directoryEnd < 3 || directoryEnd > 32)
  {
    return false;
  }
  size_t directorySize = (size_t)(directoryEnd - 2) * apple2SmartPortBlockSize;
  uint8_t *directory = malloc(directorySize);
  if (directory == NULL)
  {
    return false;
  }
  for (uint16_t block = 2; block < directoryEnd; ++block)
  {
    if (apple2DiskImageReadBlock(image, block,
                                 &directory[(size_t)(block - 2) * apple2SmartPortBlockSize]) != apple2DiskImageOk)
    {
      free(directory);
      return false;
    }
  }

  uint16_t entryCount = (uint16_t)(directory[16] | ((uint16_t)directory[17] << 8));
  size_t fileNameLength = strlen(fileName);
  bool found = false;
  for (uint16_t index = 0; index < entryCount; ++index)
  {
    size_t entryOffset = 26U * ((size_t)index + 1U);
    if (entryOffset + 26U > directorySize)
    {
      break;
    }
    const uint8_t *entry = &directory[entryOffset];
    if (entry[6] == fileNameLength && memcmp(&entry[7], fileName, fileNameLength) == 0)
    {
      found = true;
      break;
    }
  }
  free(directory);
  return found;
}

static void stagePascalTextFile(apple2DiskImage *image, const char *fileName, const char *sourcePath)
{
  uint8_t directoryHeader[apple2SmartPortBlockSize];
  assert(apple2DiskImageReadBlock(image, 2, directoryHeader) == apple2DiskImageOk);
  uint16_t directoryEnd = (uint16_t)(directoryHeader[2] | ((uint16_t)directoryHeader[3] << 8));
  uint16_t totalBlocks = (uint16_t)(directoryHeader[14] | ((uint16_t)directoryHeader[15] << 8));
  size_t directorySize = (size_t)(directoryEnd - 2) * apple2SmartPortBlockSize;
  uint8_t *directory = malloc(directorySize);
  assert(directory != NULL);
  for (uint16_t block = 2; block < directoryEnd; ++block)
  {
    assert(apple2DiskImageReadBlock(image, block,
                                    &directory[(size_t)(block - 2) * apple2SmartPortBlockSize]) ==
           apple2DiskImageOk);
  }

  uint16_t entryCount = (uint16_t)(directory[16] | ((uint16_t)directory[17] << 8));
  size_t fileNameLength = strlen(fileName);
  assert(fileNameLength > 0 && fileNameLength <= 15);
  uint16_t firstTextBlock = 0;
  for (uint16_t index = 0; index < entryCount; ++index)
  {
    size_t entryOffset = 26U * ((size_t)index + 1U);
    assert(entryOffset + 26U <= directorySize);
    uint8_t *entry = &directory[entryOffset];
    assert(!(entry[6] == fileNameLength && memcmp(&entry[7], fileName, fileNameLength) == 0));
    if ((entry[4] & 0x0F) == 3)
    {
      firstTextBlock = (uint16_t)(entry[0] | ((uint16_t)entry[1] << 8));
      break;
    }
  }
  assert(firstTextBlock != 0);

  FILE *sourceFile = fopen(sourcePath, "rb");
  assert(sourceFile != NULL);
  assert(fseek(sourceFile, 0, SEEK_END) == 0);
  long sourceLength = ftell(sourceFile);
  assert(sourceLength > 0);
  assert(fseek(sourceFile, 0, SEEK_SET) == 0);
  char *source = malloc((size_t)sourceLength + 1);
  assert(source != NULL);
  assert(fread(source, 1, (size_t)sourceLength, sourceFile) == (size_t)sourceLength);
  assert(fclose(sourceFile) == 0);
  source[sourceLength] = '\0';

  size_t textCapacity = ((size_t)sourceLength + 1024U) * 2U;
  uint8_t *text = calloc(textCapacity, 1);
  assert(text != NULL);
  uint8_t header[2 * apple2SmartPortBlockSize];
  assert(apple2DiskImageReadBlock(image, firstTextBlock, header) == apple2DiskImageOk);
  assert(apple2DiskImageReadBlock(image, firstTextBlock + 1, &header[apple2SmartPortBlockSize]) ==
         apple2DiskImageOk);
  memcpy(text, header, sizeof(header));
  size_t textLength = sizeof(header);
  const char *line = source;
  while (*line != '\0')
  {
    const char *lineEnd = line;
    while (*lineEnd != '\0' && *lineEnd != '\r' && *lineEnd != '\n')
    {
      ++lineEnd;
    }
    size_t lineLength = (size_t)(lineEnd - line);
    size_t indentation = 0;
    while (indentation < lineLength && line[indentation] == ' ')
    {
      ++indentation;
    }
    assert(indentation <= 95 && lineLength - indentation <= 255);
    size_t recordLength = lineLength - indentation + 3U;
    //-- Real Pascal text pages never split a line and always keep at least one trailing NUL.
    size_t pageOffset = (textLength - sizeof(header)) % 1024U;
    if (recordLength + 1U > 1024U - pageOffset)
    {
      textLength += 1024U - pageOffset;
    }
    assert(textLength + recordLength <= textCapacity);
    text[textLength++] = 16;
    text[textLength++] = (uint8_t)(32U + indentation);
    memcpy(&text[textLength], line + indentation, lineLength - indentation);
    textLength += lineLength - indentation;
    text[textLength++] = '\r';
    if (*lineEnd == '\r')
    {
      ++lineEnd;
      if (*lineEnd == '\n')
      {
        ++lineEnd;
      }
    }
    else if (*lineEnd == '\n')
    {
      ++lineEnd;
    }
    line = lineEnd;
  }
  textLength = (textLength + 1023U) & ~(size_t)1023U;
  size_t requiredBlocks = textLength / apple2SmartPortBlockSize;
  free(source);
  assert(requiredBlocks > 0 && requiredBlocks < totalBlocks);

  uint16_t firstBlock = directoryEnd;
  for (;;)
  {
    assert((uint32_t)firstBlock + requiredBlocks <= totalBlocks);
    bool moved = false;
    for (uint16_t index = 0; index < entryCount; ++index)
    {
      size_t entryOffset = 26U * ((size_t)index + 1U);
      const uint8_t *entry = &directory[entryOffset];
      uint16_t occupiedFirst = (uint16_t)(entry[0] | ((uint16_t)entry[1] << 8));
      uint16_t occupiedNext = (uint16_t)(entry[2] | ((uint16_t)entry[3] << 8));
      if (firstBlock < occupiedNext && (uint32_t)firstBlock + requiredBlocks > occupiedFirst)
      {
        firstBlock = occupiedNext;
        moved = true;
        break;
      }
    }
    if (!moved)
    {
      break;
    }
  }

  for (size_t index = 0; index < requiredBlocks; ++index)
  {
    assert(apple2DiskImageWriteBlock(image, firstBlock + (uint32_t)index,
                                     &text[index * apple2SmartPortBlockSize]) == apple2DiskImageOk);
  }
  free(text);

  assert(26U * ((size_t)entryCount + 2U) <= directorySize);
  uint8_t newEntry[26] = {0};
  uint16_t nextBlock = (uint16_t)(firstBlock + requiredBlocks);
  newEntry[0] = (uint8_t)firstBlock;
  newEntry[1] = (uint8_t)(firstBlock >> 8);
  newEntry[2] = (uint8_t)nextBlock;
  newEntry[3] = (uint8_t)(nextBlock >> 8);
  newEntry[4] = 3;
  newEntry[6] = (uint8_t)fileNameLength;
  memcpy(&newEntry[7], fileName, fileNameLength);
  newEntry[22] = 0;
  newEntry[23] = 2;
  memcpy(&directory[26U * ((size_t)entryCount + 1U)], newEntry, sizeof(newEntry));
  ++entryCount;
  directory[16] = (uint8_t)entryCount;
  directory[17] = (uint8_t)(entryCount >> 8);

  for (uint16_t index = 1; index < entryCount; ++index)
  {
    size_t currentOffset = 26U * ((size_t)index + 1U);
    uint8_t current[26];
    memcpy(current, &directory[currentOffset], sizeof(current));
    uint16_t currentFirst = (uint16_t)(current[0] | ((uint16_t)current[1] << 8));
    uint16_t position = index;
    while (position > 0)
    {
      size_t previousOffset = 26U * (size_t)position;
      uint16_t previousFirst = (uint16_t)(directory[previousOffset] |
                                          ((uint16_t)directory[previousOffset + 1] << 8));
      if (previousFirst <= currentFirst)
      {
        break;
      }
      memcpy(&directory[previousOffset + 26U], &directory[previousOffset], 26U);
      --position;
    }
    memcpy(&directory[26U * ((size_t)position + 1U)], current, sizeof(current));
  }

  for (uint16_t block = 2; block < directoryEnd; ++block)
  {
    assert(apple2DiskImageWriteBlock(image, block,
                                     &directory[(size_t)(block - 2) * apple2SmartPortBlockSize]) ==
           apple2DiskImageOk);
  }
  free(directory);
}

typedef struct
{
  apple2Core *core;
  apple2DiskImage bootDisk;
  apple2DiskImage toolsDisk;
  apple2DiskImage workDisk;
} pascalSession;

//-- Boots Pascal 1.3 on the Apple IIe with the boot disk, a read-only tools volume and a writable work volume.
static void pascalSessionOpen(pascalSession *session, const char *bootImagePath, bool bootWritable,
                              const char *iieRomPath, const char *toolsImagePath, const char *workImagePath)
{
  apple2DiskImageInitialize(&session->bootDisk);
  assert(apple2DiskImageOpenProfileMode(&session->bootDisk, bootImagePath, apple2DiskImageProfile140k,
                                        apple2DiskImageOrderAuto, !bootWritable) == apple2DiskImageOk);
  apple2DiskImageInitialize(&session->toolsDisk);
  assert(apple2DiskImageOpenProfileMode(&session->toolsDisk, toolsImagePath, apple2DiskImageProfile640k,
                                        apple2DiskImageOrderAuto, !bootWritable) == apple2DiskImageOk);
  apple2DiskImageInitialize(&session->workDisk);
  assert(apple2DiskImageOpenProfileMode(&session->workDisk, workImagePath, apple2DiskImageProfile640k,
                                        apple2DiskImageOrderAuto, false) == apple2DiskImageOk);

  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  session->core = core;
  assert(apple2CoreEnableIIe(core) == apple2CoreOk);
  assert(apple2CoreSetCharacterOptions(core, true, true) == apple2CoreOk);
  apple2DiskSectorOrder bootOrder = session->bootDisk.order == apple2DiskImageOrderProdos
                                       ? apple2DiskSectorOrderProdos
                                       : apple2DiskSectorOrderDos;
  assert(apple2CoreAttachWritableDiskDrive(core, 6, 0, bootOrder, session->bootDisk.trackCount,
                                           apple2DiskImageReadSectorCallback,
                                           bootWritable ? apple2DiskImageWriteSectorCallback : NULL,
                                           &session->bootDisk) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 1, (uint32_t)session->toolsDisk.trackCount * 8U,
                                         apple2DiskImageReadBlockCallback,
                                         bootWritable ? apple2DiskImageWriteBlockCallback : NULL,
                                         &session->toolsDisk) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 2, (uint32_t)session->workDisk.trackCount * 8U,
                                         apple2DiskImageReadBlockCallback,
                                         apple2DiskImageWriteBlockCallback,
                                         &session->workDisk) == apple2CoreOk);
  uint8_t rom[apple2IIeRomSize];
  FILE *file = fopen(iieRomPath, "rb");
  assert(file != NULL && fread(rom, 1, sizeof(rom), file) == sizeof(rom));
  assert(fclose(file) == 0);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);

  bool booted = false;
  for (int step = 0; step < 120 && !booted; ++step)
  {
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    booted = screenContains(core, "Command: F(ile");
  }
  if (!booted)
  {
    dumpScreen(core);
  }
  assert(booted);
}

static void pascalSessionClose(pascalSession *session)
{
  assert(apple2CoreDetachDisk(session->core) == apple2CoreOk);
  apple2CoreDestroy(session->core);
  assert(apple2DiskImageClose(&session->workDisk) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&session->toolsDisk) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&session->bootDisk) == apple2DiskImageOk);
}

//-- Runs in 1M-cycle steps until the text shows on screen; dumps the screen and fails on timeout.
static void pascalWaitFor(apple2Core *core, const char *text, unsigned maxMillionCycles)
{
  for (unsigned step = 0; step < maxMillionCycles; ++step)
  {
    if (screenContains(core, text))
    {
      return;
    }
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
  }
  dumpScreen(core);
  printf("TIMEOUT waiting for \"%s\"\n", text);
  assert(screenContains(core, text));
}

static void testPascalCompilerPrompt(const char *bootImagePath, const char *iieRomPath,
                                    const char *toolsImagePath, const char *workImagePath,
                                    const char *pascalSourcePath, const char *assemblySourcePath)
{
  char toolsScratchPath[512];
  char workScratchPath[512];
  makeScratchCopy(toolsImagePath, toolsScratchPath, sizeof(toolsScratchPath));
  makeScratchCopy(workImagePath, workScratchPath, sizeof(workScratchPath));
  pascalSession session;
  pascalSessionOpen(&session, bootImagePath, false, iieRomPath, toolsScratchPath, workScratchPath);
  assert(!pascalVolumeHasFile(&session.workDisk, "HOST.CODE"));
  stagePascalTextFile(&session.workDisk, "HOST.TEXT", pascalSourcePath);
  stagePascalTextFile(&session.workDisk, "HOSTIO.TEXT", assemblySourcePath);

  apple2Core *core = session.core;
  typeText(core, "C");
  pascalWaitFor(core, "Compile what textfile", 10);
  typeText(core, "WORK1:HOST\r");
  pascalWaitFor(core, "To what codefile", 10);
  typeText(core, "WORK1:HOST\r");
  pascalWaitFor(core, "Listing file", 10);
  typeText(core, "\r");
  for (unsigned step = 0; step < 400; ++step)
  {
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    if (screenContains(core, "error ") || screenContains(core, "Command: F(ile"))
    {
      break;
    }
  }
  dumpScreen(core);
  assert(!screenContains(core, "error "));
  assert(screenContains(core, "Command: F(ile"));

  pascalSessionClose(&session);
  apple2DiskImage workCheck;
  apple2DiskImageInitialize(&workCheck);
  assert(apple2DiskImageOpenProfileMode(&workCheck, workScratchPath, apple2DiskImageProfile640k,
                                        apple2DiskImageOrderAuto, true) == apple2DiskImageOk);
  assert(pascalVolumeHasFile(&workCheck, "HOST.CODE"));
  assert(apple2DiskImageClose(&workCheck) == apple2DiskImageOk);
  assert(unlink(workScratchPath) == 0);
  assert(unlink(toolsScratchPath) == 0);
}

static uint8_t readScriptExchangeSlot(void *context, uint8_t registerIndex)
{
  return registerIndex == 0 ? hostExchangePortInput(context, hostExchangePort) : 0xFF;
}

static void writeScriptExchangeSlot(void *context, uint8_t registerIndex, uint8_t value)
{
  if (registerIndex == 0)
  {
    hostExchangePortOutput(context, hostExchangePort, value);
  }
  else if (registerIndex == 1)
  {
    hostExchangePortOutput(context, hostExchangeAbortPort, value);
  }
}

//-- Development driver: stages HOST sources on private /tmp copies and plays a keystroke script.
//-- Script lines: "type TEXT" (\r = return), "wait N TEXT" (N million cycles), "run N", "dump".
static void testPascalScript(const char *bootImagePath, const char *iieRomPath,
                             const char *toolsImagePath, const char *workImagePath,
                             const char *pascalSourcePath, const char *assemblySourcePath,
                             const char *scriptPath, const char *workOutPath)
{
  size_t toolsSize;
  uint8_t *toolsData = readWholeFile(toolsImagePath, &toolsSize);
  size_t workSize;
  uint8_t *workData = readWholeFile(workImagePath, &workSize);
  const char *toolsTmp = "/tmp/ucsd-script-tools.po";
  FILE *copy = fopen(toolsTmp, "wb");
  assert(copy != NULL && fwrite(toolsData, 1, toolsSize, copy) == toolsSize && fclose(copy) == 0);
  copy = fopen(workOutPath, "wb");
  assert(copy != NULL && fwrite(workData, 1, workSize, copy) == workSize && fclose(copy) == 0);
  free(toolsData);
  free(workData);

  const char *bootTmp = "/tmp/ucsd-script-boot.dsk";
  size_t bootSize;
  uint8_t *bootData = readWholeFile(bootImagePath, &bootSize);
  copy = fopen(bootTmp, "wb");
  assert(copy != NULL && fwrite(bootData, 1, bootSize, copy) == bootSize && fclose(copy) == 0);
  free(bootData);
  pascalSession session;
  pascalSessionOpen(&session, bootTmp, true, iieRomPath, toolsTmp, workOutPath);
  if (getenv("UCSD_SKIP_STAGE") == NULL)
  {
    stagePascalTextFile(&session.workDisk, "HOST.TEXT", pascalSourcePath);
    stagePascalTextFile(&session.workDisk, "HOSTIO.TEXT", assemblySourcePath);
  }
  //-- UCSD_STAGE_EXTRA="NAME.TEXT=/path/source" stages one additional experiment source.
  const char *extraStage = getenv("UCSD_STAGE_EXTRA");
  if (extraStage != NULL)
  {
    char extraName[64];
    const char *separator = strchr(extraStage, '=');
    assert(separator != NULL && (size_t)(separator - extraStage) < sizeof(extraName));
    memcpy(extraName, extraStage, (size_t)(separator - extraStage));
    extraName[separator - extraStage] = '\0';
    stagePascalTextFile(&session.workDisk, extraName, separator + 1);
  }

  //-- When UCSD_EXCHANGE_DIR is set, the slot-2 exchange bridge serves that directory.
  hostExchange exchange = {0};
  const char *exchangeDirectory = getenv("UCSD_EXCHANGE_DIR");
  if (exchangeDirectory != NULL)
  {
    assert(hostExchangeInitializeUcsd(&exchange, exchangeDirectory));
    assert(apple2CoreAttachSlotIoDevice(session.core, 2, readScriptExchangeSlot, writeScriptExchangeSlot,
                                        &exchange) == apple2CoreOk);
  }

  FILE *script = fopen(scriptPath, "r");
  assert(script != NULL);
  char line[512];
  while (fgets(line, sizeof(line), script) != NULL)
  {
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
    {
      line[--length] = '\0';
    }
    if (strncmp(line, "type ", 5) == 0)
    {
      char text[512];
      size_t out = 0;
      for (const char *in = line + 5; *in != '\0'; ++in)
      {
        if (in[0] == '\\' && in[1] == 'r')
        {
          text[out++] = '\r';
          ++in;
        }
        else
        {
          text[out++] = *in;
        }
      }
      text[out] = '\0';
      typeText(session.core, text);
    }
    else if (strncmp(line, "wait ", 5) == 0)
    {
      unsigned maxMillion = (unsigned)strtoul(line + 5, NULL, 10);
      const char *text = strchr(line + 5, ' ');
      assert(text != NULL);
      pascalWaitFor(session.core, text + 1, maxMillion);
    }
    else if (strncmp(line, "run ", 4) == 0)
    {
      unsigned million = (unsigned)strtoul(line + 4, NULL, 10);
      for (unsigned step = 0; step < million; ++step)
      {
        assert(apple2CoreRunCycles(session.core, 1000000) == apple2CoreOk);
      }
    }
    else if (strcmp(line, "dump") == 0)
    {
      dumpScreen(session.core);
      apple2DiskState diskState;
      apple2CoreGetDiskState(session.core, &diskState);
      printf("---- disk6: reads=%u readFail=%u writes=%u writeFail=%u\n", (unsigned)diskState.sectorReads,
             (unsigned)diskState.sectorReadFailures, (unsigned)diskState.writeAttempts,
             (unsigned)diskState.writeFailures);
    }
  }
  assert(fclose(script) == 0);
  if (exchangeDirectory != NULL)
  {
    hostExchangeClose(&exchange);
  }
  pascalSessionClose(&session);
}

//-- Pascal must enumerate three block volumes across slots and open a TEXT file from slot 5 unit 2.
static void testPascalIIeEditorOpen(const char *bootImagePath, const char *iieRomPath,
                                    const char *systemImagePath, const char *workImagePath,
                                    const char *emptyWorkImagePath)
{
  char systemScratchPath[512];
  char scratchPath[512];
  char emptyWorkScratchPath[512];
  makeScratchCopy(systemImagePath, systemScratchPath, sizeof(systemScratchPath));
  makeScratchCopy(workImagePath, scratchPath, sizeof(scratchPath));
  makeScratchCopy(emptyWorkImagePath, emptyWorkScratchPath, sizeof(emptyWorkScratchPath));
  apple2DiskImage bootDisk;
  apple2DiskImageInitialize(&bootDisk);
  assert(apple2DiskImageOpenProfileMode(&bootDisk, bootImagePath, apple2DiskImageProfile140k,
                                        apple2DiskImageOrderAuto, true) == apple2DiskImageOk);
  apple2DiskImage systemDisk;
  apple2DiskImageInitialize(&systemDisk);
  assert(apple2DiskImageOpenProfileMode(&systemDisk, systemScratchPath, apple2DiskImageProfile640k,
                                        apple2DiskImageOrderAuto, false) == apple2DiskImageOk);
  apple2DiskImage workDisk;
  apple2DiskImageInitialize(&workDisk);
  assert(apple2DiskImageOpenProfileMode(&workDisk, scratchPath, apple2DiskImageProfile640k,
                                        apple2DiskImageOrderAuto, false) == apple2DiskImageOk);
  apple2DiskImage emptyWorkDisk;
  apple2DiskImageInitialize(&emptyWorkDisk);
  assert(apple2DiskImageOpenProfileMode(&emptyWorkDisk, emptyWorkScratchPath, apple2DiskImageProfile640k,
                                        apple2DiskImageOrderAuto, false) == apple2DiskImageOk);
  assert(systemDisk.probe.content == apple2DiskImageContentPascal);
  assert(workDisk.probe.content == apple2DiskImageContentPascal);
  assert(emptyWorkDisk.probe.content == apple2DiskImageContentPascal);

  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  assert(apple2CoreEnableIIe(core) == apple2CoreOk);
  assert(apple2CoreSetCharacterOptions(core, true, true) == apple2CoreOk);
  apple2DiskSectorOrder bootOrder = bootDisk.order == apple2DiskImageOrderProdos
                                       ? apple2DiskSectorOrderProdos
                                       : apple2DiskSectorOrderDos;
  assert(apple2CoreAttachDiskDrive(core, 6, 0, bootOrder, bootDisk.trackCount,
                                   apple2DiskImageReadSectorCallback, &bootDisk) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 1, (uint32_t)systemDisk.trackCount * 8U,
                                         apple2DiskImageReadBlockCallback,
                                         NULL, &systemDisk) == apple2CoreOk);
  assert(apple2CoreAttachSmartPortDevice(core, 2, (uint32_t)workDisk.trackCount * 8U,
                                         apple2DiskImageReadBlockCallback,
                                         apple2DiskImageWriteBlockCallback, &workDisk) == apple2CoreOk);
  assert(apple2CoreAttachBlockDevice(core, 4, 1, (uint32_t)emptyWorkDisk.trackCount * 8U, false,
                                     apple2DiskImageReadBlockCallback,
                                     apple2DiskImageWriteBlockCallback, &emptyWorkDisk) == apple2CoreOk);
  uint8_t rom[apple2IIeRomSize];
  FILE *file = fopen(iieRomPath, "rb");
  assert(file != NULL && fread(rom, 1, sizeof(rom), file) == sizeof(rom));
  fclose(file);
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);

  bool booted = false;
  for (int step = 0; step < 120 && !booted; ++step)
  {
    assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
    booted = screenContains(core, "Command: F(ile");
  }
  assert(booted);
  typeText(core, "F");
  assert(apple2CoreRunCycles(core, 10000000) == apple2CoreOk);
  typeText(core, "V");
  assert(apple2CoreRunCycles(core, 10000000) == apple2CoreOk);
  assert(screenContains(core, "DATA3:"));
  assert(screenContains(core, "WORK1:"));
  assert(screenContains(core, "WORK:"));
  typeText(core, "Q");
  assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
  typeText(core, "E");
  assert(apple2CoreRunCycles(core, 1000000) == apple2CoreOk);
  typeText(core, "WORK1:SPIRODEMO.TEXT\r");
  assert(apple2CoreRunCycles(core, 10000000) == apple2CoreOk);
  if (screenContains(core, "ERROR: Reading file"))
  {
    dumpScreen(core);
  }
  if (!screenContains(core, "PROGRAM SPIRODEMO;"))
  {
    dumpScreen(core);
  }
  assert(!screenContains(core, "ERROR: Reading file"));
  assert(screenContains(core, "PROGRAM SPIRODEMO;"));

  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreDestroy(core);
  assert(apple2DiskImageClose(&emptyWorkDisk) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&workDisk) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&systemDisk) == apple2DiskImageOk);
  assert(apple2DiskImageClose(&bootDisk) == apple2DiskImageOk);
  assert(unlink(scratchPath) == 0);
  assert(unlink(systemScratchPath) == 0);
  assert(unlink(emptyWorkScratchPath) == 0);
}

int main(int argc, char **argv)
{
  if (argc == 4 && strcmp(argv[1], "--pascal-iie-boot") == 0)
  {
    testPascalIIeBoot(argv[2], argv[3], apple2DiskImageProfile140k, false);
    puts("PASS: Apple IIe boots the 128K Apple Pascal system");
    return 0;
  }
  if (argc == 4 && strcmp(argv[1], "--pascal-iie-boot-800k-block") == 0)
  {
    testPascalIIeBoot(argv[2], argv[3], apple2DiskImageProfile800k, true);
    puts("PASS: Apple IIe boots the 64K Apple Pascal system from an 800K block device");
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--writable-800k-block-image") == 0)
  {
    testWritableBlockImage800k();
    puts("PASS: writable 800K block image persists its final block and rejects out-of-range access");
    return 0;
  }
  if (argc == 7 && strcmp(argv[1], "--pascal-iie-editor-open") == 0)
  {
    testPascalIIeEditorOpen(argv[2], argv[3], argv[4], argv[5], argv[6]);
    puts("PASS: Pascal lists three SmartPort volumes and opens a TEXT file from WORK1");
    return 0;
  }
  if (argc == 8 && strcmp(argv[1], "--pascal-compiler-prompt") == 0)
  {
    testPascalCompilerPrompt(argv[2], argv[3], argv[4], argv[5], argv[6], argv[7]);
    puts("PASS: Apple Pascal compiles HOST.TEXT into HOST.CODE");
    return 0;
  }
  if (argc == 10 && strcmp(argv[1], "--pascal-script") == 0)
  {
    testPascalScript(argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9]);
    puts("DONE: Pascal script finished");
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--pascal-boot") == 0)
  {
    testPascalBoot(argv[2]);
    puts("PASS: Apple Pascal 1 boots to its command line from SD6.1");
    return 0;
  }
  if (argc == 4 && strcmp(argv[1], "--prodos-smartport-boot") == 0)
  {
    testProDosSmartPortBoot(argv[2], argv[3]);
    puts("PASS: ProDOS boot, SmartPort catalog and BASIC SAVE to the 800K volume");
    return 0;
  }
  if (argc == 4 && strcmp(argv[1], "--prodos-autostart-boot") == 0)
  {
    testProDosAutostartBoot(argv[2], argv[3], false);
    puts("PASS: ProDOS autostart from SD6.1, catalog and BASIC SAVE to the SD6.2 800K volume");
    return 0;
  }
  if (argc == 4 && strcmp(argv[1], "--prodos-80col-boot") == 0)
  {
    testProDosAutostartBoot(argv[2], argv[3], true);
    puts("PASS: ProDOS autostart with the 80-column card");
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--disk-stream") == 0)
  {
    testStreamMatchesImage(APPLE2_SYSTEM_DISK_PATH);
    puts("PASS: Disk II nibble stream equals every sector of the system image");
    return 0;
  }
  testImageOpenAndBounds();
  testWritableDiskImage();
  testWritableBlockImage800k();
  testControllerStream();
  testControllerFailureAndHalfTrack();
  testMotorCoastDown();
  testSoftSwitchesThroughCore();
  testSmartPortBlockCall();
  testGuestBootSector();
  puts("PASS: Disk II image, SmartPort block calls, controller stream, softswitches and guest boot-sector read");
  return 0;
}
