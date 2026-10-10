#include "apple2Core.h"
#include "apple2Disk.h"
#include "apple2DiskImage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//-- Disk II (slot 6, drive 1, read-only, DOS 3.3 16-sector) host tests.
//-- Synthetic data is used for the low-level tests; the generated system.dsk is used for the DOS tests.

enum
{
  imageBytes = apple2DiskImageSize
};

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
  while (disk->halfTrack < halfTrack)
  {
    uint8_t phase = (uint8_t)((disk->halfTrack + 1) & 3);
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
    assert(disk.halfTrack == track * 2);
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
  uint8_t previousHalfTrack = disk.halfTrack;
  for (int count = 0; count < 12; ++count)
  {
    uint8_t phase = (uint8_t)((disk.halfTrack + 1) & 3);
    apple2DiskAccess(&disk, (uint8_t)(phase * 2 + 1), false, 0, 0);
    apple2DiskAccess(&disk, (uint8_t)(phase * 2), false, 0, 0);
    assert(disk.halfTrack >= previousHalfTrack);
    assert(disk.halfTrack <= apple2DiskMaxHalfTrack);
  }
  assert(disk.halfTrack == apple2DiskMaxHalfTrack);
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
  assert(disk.halfTrack == 1);
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

  //-- Writes: attempting to enter write mode is counted and nothing reaches the media.
  const uint32_t readsBefore = synthetic.calls;
  assert(apple2CoreWriteMemory(core, 0xC0ED, 0xFF) == apple2CoreOk);
  assert(apple2CoreWriteMemory(core, 0xC0EF, 0xFF) == apple2CoreOk);
  assert(apple2CoreWriteMemory(core, 0xC0ED, 0xAA) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.q6 && state.q7 && state.writeAttempts >= 1);
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

int main(int argc, char **argv)
{
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
  testControllerStream();
  testControllerFailureAndHalfTrack();
  testMotorCoastDown();
  testSoftSwitchesThroughCore();
  testGuestBootSector();
  puts("PASS: Disk II image, controller stream, softswitches and guest boot-sector read");
  return 0;
}
