#include "apple2Core.h"
#include "apple2Disk.h"
#include "apple2DiskImage.h"
#include "apple2DriveConfig.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//-- Host tests for several drives / controllers, 640K media, ProDOS/Apple Pascal sector order,
//-- Pascal and DOS 3.3 content probing and the Apple II drives.cfg parser.

enum
{
  sectorSize = 256,
  sectorsPerTrack = 16,
  pascalBlockBytes = 512,
  blocks640k = 1280,
  blocks140k = 280
};

//-- DOS 3.3 interleave: logical sector -> physical sector (the standard published table).
static const uint8_t dosPhysicalOfLogical[16] = {0, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 15};
//-- ProDOS: block n of a track occupies these two physical sectors (Beneath Apple ProDOS block table).
static const uint8_t prodosBlockSectors[8][2] = {{0, 2}, {4, 6}, {8, 10}, {12, 14}, {1, 3}, {5, 7}, {9, 11}, {13, 15}};

static uint8_t dosLogicalOfPhysical[16];
static uint8_t prodosLogicalOfPhysical[16];
static uint8_t prodosPhysicalOfLogical[16];

static void buildOrderTables(void)
{
  for (uint8_t logical = 0; logical < 16; ++logical)
  {
    dosLogicalOfPhysical[dosPhysicalOfLogical[logical]] = logical;
  }
  for (uint8_t block = 0; block < 8; ++block)
  {
    for (uint8_t half = 0; half < 2; ++half)
    {
      uint8_t logical = (uint8_t)(block * 2 + half);
      prodosPhysicalOfLogical[logical] = prodosBlockSectors[block][half];
      prodosLogicalOfPhysical[prodosBlockSectors[block][half]] = logical;
    }
  }
}

static uint8_t imageSectorOfPhysical(apple2DiskSectorOrder order, uint8_t physical)
{
  return order == apple2DiskSectorOrderProdos ? prodosLogicalOfPhysical[physical] : dosLogicalOfPhysical[physical];
}

static uint8_t patternByte(uint8_t id, uint8_t track, uint8_t sector, size_t offset)
{
  return (uint8_t)(id * 61U + track * 31U + sector * 7U + offset * 13U + (offset >> 3));
}

typedef struct
{
  uint8_t id;
  uint32_t calls;
} syntheticDisk;

static bool syntheticRead(void *context, uint8_t track, uint8_t sector, uint8_t *buffer)
{
  syntheticDisk *disk = context;
  disk->calls++;
  for (size_t offset = 0; offset < sectorSize; ++offset)
  {
    buffer[offset] = patternByte(disk->id, track, sector, offset);
  }
  return true;
}

//-- Independent description of a valid 6-and-2 disk byte (see apple2DiskTests.c).
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
}

static uint8_t decodeFourAndFour(uint8_t first, uint8_t second)
{
  return (uint8_t)(((first << 1) | 1U) & second);
}

//-- Decodes one full track from the nibble stream; returns the bitmask of the physical sectors found.
static unsigned decodeTrack(const uint8_t *stream, uint8_t expectedTrack, uint8_t sectors[16][256])
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
    uint8_t headerTrack = decodeFourAndFour(stream[(at + 2) % length], stream[(at + 3) % length]);
    uint8_t headerSector = decodeFourAndFour(stream[(at + 4) % length], stream[(at + 5) % length]);
    assert(headerTrack == expectedTrack && headerSector < 16);
    size_t data = at + 11;
    while (stream[data % length] != 0xD5)
    {
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

static void readTrackStream(apple2Disk *disk, uint8_t *stream)
{
  for (uint32_t index = 0; index < apple2DiskNibblesPerTrack; ++index)
  {
    stream[index] = apple2DiskAccess(disk, 0xC, false, (uint64_t)index * apple2DiskCyclesPerNibble + 5, 0);
  }
}

static uint16_t selectedHalfTrack(const apple2Disk *disk)
{
  return disk->drives[disk->drive2Selected ? 1 : 0].halfTrack;
}

//-- Pulls the head outward by the given number of half-track steps (the model clamps at the last track).
static void stepHalfTracksUp(apple2Disk *disk, unsigned steps)
{
  for (unsigned step = 0; step < steps; ++step)
  {
    uint8_t phase = (uint8_t)((selectedHalfTrack(disk) + 1) & 3);
    apple2DiskAccess(disk, (uint8_t)(phase * 2 + 1), false, 0, 0);
    apple2DiskAccess(disk, (uint8_t)(phase * 2), false, 0, 0);
  }
}

static void stepToHalfTrack(apple2Disk *disk, uint16_t halfTrack)
{
  assert(selectedHalfTrack(disk) <= halfTrack);
  stepHalfTracksUp(disk, (unsigned)(halfTrack - selectedHalfTrack(disk)));
}

static void selectDrive(apple2Disk *disk, uint8_t drive)
{
  apple2DiskAccess(disk, drive == 0 ? 0xA : 0xB, false, 0, 0);
}

//-- Decodes the current track of the selected drive and checks every sector against the pattern the
//-- synthetic medium holds for that physical sector in the given image order.
static void expectTrackPattern(apple2Disk *disk, uint8_t id, uint8_t track, apple2DiskSectorOrder order)
{
  static uint8_t stream[apple2DiskNibblesPerTrack];
  uint8_t sectors[16][256];
  readTrackStream(disk, stream);
  assert(decodeTrack(stream, track, sectors) == 0xFFFFU);
  for (uint8_t physical = 0; physical < 16; ++physical)
  {
    uint8_t imageSector = imageSectorOfPhysical(order, physical);
    for (size_t offset = 0; offset < sectorSize; ++offset)
    {
      assert(sectors[physical][offset] == patternByte(id, track, imageSector, offset));
    }
  }
}

static void testOrderTables(void)
{
  assert(dosLogicalOfPhysical[1] == 7 && dosLogicalOfPhysical[13] == 1);
  assert(prodosLogicalOfPhysical[0] == 0 && prodosLogicalOfPhysical[2] == 1 && prodosLogicalOfPhysical[1] == 8);
  for (uint8_t logical = 0; logical < 16; ++logical)
  {
    assert(prodosLogicalOfPhysical[prodosPhysicalOfLogical[logical]] == logical);
  }
}

static void testAttachValidation(void)
{
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  syntheticDisk synthetic = {1, 0};
  assert(!apple2DiskAttachDrive(&disk, 2, apple2DiskSectorOrderDos, 35, syntheticRead, &synthetic));
  assert(!apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, 0, syntheticRead, &synthetic));
  assert(!apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, apple2DiskMaxTracks + 1, syntheticRead, &synthetic));
  assert(!apple2DiskAttachDrive(&disk, 0, (apple2DiskSectorOrder)7, 35, syntheticRead, &synthetic));
  assert(!apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, 35, NULL, &synthetic));
  assert(!apple2DiskIsAttached(&disk));
  assert(apple2DiskAttachDrive(&disk, 1, apple2DiskSectorOrderProdos, apple2DiskMaxTracks, syntheticRead, &synthetic));
  assert(apple2DiskIsAttached(&disk) && !disk.drives[0].attached && disk.drives[1].attached);
  apple2DiskDetachDrive(&disk, 1);
  assert(!apple2DiskIsAttached(&disk));
}

static void testSectorOrders(void)
{
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  syntheticDisk dosMedium = {1, 0};
  assert(apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, 35, syntheticRead, &dosMedium));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);
  expectTrackPattern(&disk, 1, 0, apple2DiskSectorOrderDos);
  stepToHalfTrack(&disk, 34);
  expectTrackPattern(&disk, 1, 17, apple2DiskSectorOrderDos);

  apple2DiskInitialize(&disk);
  syntheticDisk prodosMedium = {2, 0};
  assert(apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderProdos, 35, syntheticRead, &prodosMedium));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);
  expectTrackPattern(&disk, 2, 0, apple2DiskSectorOrderProdos);
  stepToHalfTrack(&disk, 34);
  expectTrackPattern(&disk, 2, 17, apple2DiskSectorOrderProdos);
}

static void testExtendedTracks(void)
{
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  syntheticDisk medium = {3, 0};
  assert(apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderProdos, 160, syntheticRead, &medium));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);
  stepToHalfTrack(&disk, 2 * 100);
  expectTrackPattern(&disk, 3, 100, apple2DiskSectorOrderProdos);
  stepToHalfTrack(&disk, 2 * 159);
  expectTrackPattern(&disk, 3, 159, apple2DiskSectorOrderProdos);
  stepHalfTracksUp(&disk, 200);
  assert(selectedHalfTrack(&disk) == 319);
  assert(apple2DiskAccess(&disk, 0xC, false, 100, 0) == 0xFF);

  //-- A 35-track medium still stops at half-track 69.
  apple2DiskInitialize(&disk);
  assert(apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, 35, syntheticRead, &medium));
  stepHalfTracksUp(&disk, 200);
  assert(selectedHalfTrack(&disk) == apple2DiskMaxHalfTrack);

  //-- Attaching a shorter medium pulls the head back onto it.
  apple2DiskInitialize(&disk);
  assert(apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, 160, syntheticRead, &medium));
  stepToHalfTrack(&disk, 200);
  assert(apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, 35, syntheticRead, &medium));
  assert(selectedHalfTrack(&disk) == apple2DiskMaxHalfTrack);
}

static void testTwoDrives(void)
{
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  syntheticDisk first = {4, 0};
  syntheticDisk second = {5, 0};
  assert(apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, 35, syntheticRead, &first));
  assert(apple2DiskAttachDrive(&disk, 1, apple2DiskSectorOrderProdos, 160, syntheticRead, &second));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);

  selectDrive(&disk, 1);
  stepToHalfTrack(&disk, 200);
  expectTrackPattern(&disk, 5, 100, apple2DiskSectorOrderProdos);
  assert(disk.drives[0].halfTrack == 0 && disk.drives[1].halfTrack == 200);

  selectDrive(&disk, 0);
  expectTrackPattern(&disk, 4, 0, apple2DiskSectorOrderDos);
  assert(first.calls > 0 && second.calls > 0);

  //-- Write-protect sense (Q6 set, Q7 clear): an attached drive is read-only, an empty drive reads zero.
  apple2DiskAccess(&disk, 0xD, false, 0, 0);
  assert(apple2DiskAccess(&disk, 0xE, false, 0, 0) == 0x80);
  selectDrive(&disk, 1);
  assert(apple2DiskAccess(&disk, 0xE, false, 0, 0) == 0x80);
  apple2DiskDetachDrive(&disk, 1);
  assert(apple2DiskAccess(&disk, 0xE, false, 0, 0) == 0x00);
  selectDrive(&disk, 0);
  assert(apple2DiskAccess(&disk, 0xE, false, 0, 0) == 0x80);
}

static void testEmptySecondDrive(void)
{
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  syntheticDisk first = {6, 0};
  assert(apple2DiskAttachDrive(&disk, 0, apple2DiskSectorOrderDos, 35, syntheticRead, &first));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);
  selectDrive(&disk, 1);
  assert(apple2DiskAccess(&disk, 0xC, false, 1000, 0) == 0x00);
  assert(apple2DiskAccess(&disk, 0xC, false, 1000 + apple2DiskCyclesPerNibble, 0) == 0x00);
  selectDrive(&disk, 0);
  expectTrackPattern(&disk, 6, 0, apple2DiskSectorOrderDos);
}

static void testSlotsThroughCore(void)
{
  apple2Core *core = NULL;
  assert(apple2CoreCreate(&core) == apple2CoreOk);
  uint8_t rom[apple2RomSize];
  memset(rom, 0xEA, sizeof(rom));
  rom[0x2FFC] = 0x00;
  rom[0x2FFD] = 0xD0;
  assert(apple2CoreLoadRom(core, rom, sizeof(rom)) == apple2CoreOk);

  syntheticDisk medium = {7, 0};
  assert(apple2CoreAttachDiskDrive(NULL, 5, 0, apple2DiskSectorOrderDos, 35, syntheticRead, &medium) ==
         apple2CoreInvalidArgument);
  assert(apple2CoreAttachDiskDrive(core, 3, 0, apple2DiskSectorOrderDos, 35, syntheticRead, &medium) ==
         apple2CoreInvalidArgument);
  assert(apple2CoreAttachDiskDrive(core, 8, 0, apple2DiskSectorOrderDos, 35, syntheticRead, &medium) ==
         apple2CoreInvalidArgument);
  assert(apple2CoreAttachDiskDrive(core, 5, 2, apple2DiskSectorOrderDos, 35, syntheticRead, &medium) ==
         apple2CoreInvalidArgument);
  assert(apple2CoreAttachDiskDrive(core, 5, 0, apple2DiskSectorOrderDos, 0, syntheticRead, &medium) ==
         apple2CoreInvalidArgument);
  apple2DiskState state;
  assert(!apple2CoreGetDiskStateForSlot(core, 3, &state));
  assert(!apple2CoreGetDiskStateForSlot(core, 8, &state));

  uint8_t value;
  assert(apple2CoreReadMemory(core, 0xC501, &value) == apple2CoreOk && value != 0x20);
  assert(apple2CoreAttachDiskDrive(core, 5, 1, apple2DiskSectorOrderProdos, 160, syntheticRead, &medium) ==
         apple2CoreOk);
  assert(apple2CoreGetDiskStateForSlot(core, 5, &state) && state.attached);
  assert(apple2CoreGetDiskStateForSlot(core, 6, &state) && !state.attached);

  //-- Slot 5 answers at $C500 and $C0D0; slot 6 stays silent and boots from nothing.
  assert(apple2CoreReadMemory(core, 0xC501, &value) == apple2CoreOk && value == 0x20);
  assert(apple2CoreReadMemory(core, 0xC503, &value) == apple2CoreOk && value == 0x00);
  assert(apple2CoreReadMemory(core, 0xC505, &value) == apple2CoreOk && value == 0x03);
  assert(apple2CoreReadMemory(core, 0xC507, &value) == apple2CoreOk && value == 0x3C);
  assert(apple2CoreReadMemory(core, 0xC509, &value) == apple2CoreOk && value == 0x50);
  assert(apple2CoreReadMemory(core, 0xC601, &value) == apple2CoreOk && value != 0x20);

  assert(apple2CoreReadMemory(core, 0xC0D9, &value) == apple2CoreOk);
  assert(apple2CoreGetDiskStateForSlot(core, 5, &state) && state.motorOn);
  assert(apple2CoreGetDiskStateForSlot(core, 6, &state) && !state.motorOn);
  assert(apple2CoreReadMemory(core, 0xC0E9, &value) == apple2CoreOk);
  assert(apple2CoreGetDiskStateForSlot(core, 6, &state) && !state.motorOn);

  //-- A refused write on slot 5 is counted on slot 5 only; the callback is never asked to write.
  assert(apple2CoreReadMemory(core, 0xC0DF, &value) == apple2CoreOk);
  assert(apple2CoreReadMemory(core, 0xC0DD, &value) == apple2CoreOk);
  assert(apple2CoreWriteMemory(core, 0xC0DD, 0x55) == apple2CoreOk);
  assert(apple2CoreGetDiskStateForSlot(core, 5, &state) && state.writeAttempts == 1);
  assert(apple2CoreGetDiskStateForSlot(core, 6, &state) && state.writeAttempts == 0);

  //-- Slot 6 with a second controller present: both attach independently, the legacy calls address slot 6.
  assert(apple2CoreAttachDisk(core, syntheticRead, &medium) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(state.attached);
  assert(apple2CoreReadMemory(core, 0xC609, &value) == apple2CoreOk && value == 0x60);

  assert(apple2CoreDetachDiskDrive(core, 5, 1) == apple2CoreOk);
  assert(apple2CoreGetDiskStateForSlot(core, 5, &state) && !state.attached && !state.motorOn);
  assert(apple2CoreReadMemory(core, 0xC501, &value) == apple2CoreOk && value != 0x20);
  assert(apple2CoreDetachDiskDrive(core, 5, 2) == apple2CoreInvalidArgument);
  assert(apple2CoreDetachDisk(core) == apple2CoreOk);
  apple2CoreGetDiskState(core, &state);
  assert(!state.attached);
  apple2CoreDestroy(core);
}

static void makeTempPath(char *path, size_t size, const char *suffix)
{
  snprintf(path, size, "/tmp/apple2MultiDiskXXXXXX%s", suffix);
  int descriptor = mkstemps(path, (int)strlen(suffix));
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

static void putWord(uint8_t *target, uint16_t value)
{
  target[0] = (uint8_t)(value & 0xFF);
  target[1] = (uint8_t)(value >> 8);
}

//-- Builds a ProDOS-order image (block n at byte n * 512) holding a minimal Apple Pascal volume.
static void buildPascalProdosImage(uint8_t *image, uint16_t volumeBlocks, const char *name)
{
  uint8_t *header = &image[2 * pascalBlockBytes];
  putWord(&header[0], 0);
  putWord(&header[2], 6);
  putWord(&header[4], 0);
  header[6] = (uint8_t)strlen(name);
  memcpy(&header[7], name, strlen(name));
  putWord(&header[0x0E], volumeBlocks);
  putWord(&header[0x10], 0);
}

//-- Re-stores a ProDOS-order image in DOS order: the sector with ProDOS logical number L moves to the
//-- file position DOS logical number of the same physical sector.
static void convertProdosToDosOrder(const uint8_t *prodos, uint8_t *dos, uint8_t tracks)
{
  for (uint8_t track = 0; track < tracks; ++track)
  {
    for (uint8_t logical = 0; logical < 16; ++logical)
    {
      uint8_t physical = prodosPhysicalOfLogical[logical];
      memcpy(&dos[((size_t)track * 16 + dosLogicalOfPhysical[physical]) * sectorSize],
             &prodos[((size_t)track * 16 + logical) * sectorSize], sectorSize);
    }
  }
}

//-- DOS 3.3 VTOC (track 17 sector 0) and the first two catalog sectors (track 17 sectors 15 and 14), written in
//-- the file order of the image.
static void buildDos33Image(uint8_t *image, apple2DiskSectorOrder order)
{
  uint8_t vtoc[sectorSize] = {0};
  vtoc[1] = 17;
  vtoc[2] = 15;
  vtoc[3] = 3;
  vtoc[6] = 254;
  vtoc[0x27] = 122;
  vtoc[0x34] = 35;
  vtoc[0x35] = 16;
  uint8_t catalog[sectorSize] = {0};
  catalog[1] = 17;
  catalog[2] = 14;
  uint8_t secondCatalog[sectorSize] = {0};
  secondCatalog[1] = 17;
  secondCatalog[2] = 13;
  uint8_t vtocFileSector = imageSectorOfPhysical(order, dosPhysicalOfLogical[0]);
  uint8_t catalogFileSector = imageSectorOfPhysical(order, dosPhysicalOfLogical[15]);
  memcpy(&image[(17 * 16 + (size_t)vtocFileSector) * sectorSize], vtoc, sectorSize);
  memcpy(&image[(17 * 16 + (size_t)catalogFileSector) * sectorSize], catalog, sectorSize);
  uint8_t secondFileSector = imageSectorOfPhysical(order, dosPhysicalOfLogical[14]);
  memcpy(&image[(17 * 16 + (size_t)secondFileSector) * sectorSize], secondCatalog, sectorSize);
}

static void testImageProfileSizes(void)
{
  uint8_t *small = calloc(apple2DiskImageSize, 1);
  uint8_t *big = calloc(apple2DiskImage640kSize, 1);
  assert(small != NULL && big != NULL);
  char smallPath[64];
  char bigPath[64];
  makeTempPath(smallPath, sizeof(smallPath), ".dsk");
  makeTempPath(bigPath, sizeof(bigPath), ".po");
  writeFile(smallPath, small, apple2DiskImageSize);
  writeFile(bigPath, big, apple2DiskImage640kSize);

  apple2DiskImage image;
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageProfileTracks(apple2DiskImageProfile140k) == 35);
  assert(apple2DiskImageProfileTracks(apple2DiskImageProfile640k) == 160);
  assert(apple2DiskImageOpenProfile(NULL, smallPath, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
         apple2DiskImageInvalidArgument);
  assert(apple2DiskImageOpenProfile(&image, NULL, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
         apple2DiskImageInvalidArgument);
  assert(apple2DiskImageOpenProfile(&image, smallPath, apple2DiskImageProfile640k, apple2DiskImageOrderAuto) ==
         apple2DiskImageBadSize);
  assert(apple2DiskImageOpenProfile(&image, bigPath, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
         apple2DiskImageBadSize);
  assert(apple2DiskImageOpenProfile(&image, "/tmp/apple2MultiDisk-does-not-exist", apple2DiskImageProfile140k,
                                    apple2DiskImageOrderAuto) == apple2DiskImageNotFound);
  assert(!apple2DiskImageIsOpen(&image));

  assert(apple2DiskImageOpenProfile(&image, bigPath, apple2DiskImageProfile640k, apple2DiskImageOrderAuto) ==
         apple2DiskImageOk);
  assert(image.trackCount == 160 && image.order == apple2DiskImageOrderProdos);
  assert(image.probe.content == apple2DiskImageContentUnknown && !image.orderSuspect);
  uint8_t buffer[sectorSize];
  assert(apple2DiskImageReadSector(&image, 159, 15, buffer) == apple2DiskImageOk);
  assert(apple2DiskImageReadSector(&image, 160, 0, buffer) == apple2DiskImageOutOfRange);
  assert(apple2DiskImageOpenProfile(&image, bigPath, apple2DiskImageProfile640k, apple2DiskImageOrderAuto) ==
         apple2DiskImageAlreadyOpen);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);

  //-- Unrecognised content with a neutral name defaults to DOS order; explicit orders win.
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, smallPath, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
         apple2DiskImageOk);
  assert(image.order == apple2DiskImageOrderDos && !image.probe.orderKnown);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, smallPath, apple2DiskImageProfile140k, apple2DiskImageOrderProdos) ==
         apple2DiskImageOk);
  assert(image.order == apple2DiskImageOrderProdos);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);

  free(small);
  free(big);
  assert(unlink(smallPath) == 0);
  assert(unlink(bigPath) == 0);
}

static void testPascalProbe(void)
{
  //-- 640K Apple Pascal volume stored in ProDOS order (.po).
  uint8_t *big = calloc(apple2DiskImage640kSize, 1);
  assert(big != NULL);
  buildPascalProdosImage(big, blocks640k, "BIGVOL");
  char bigPath[64];
  makeTempPath(bigPath, sizeof(bigPath), ".po");
  writeFile(bigPath, big, apple2DiskImage640kSize);

  apple2DiskImage image;
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, bigPath, apple2DiskImageProfile640k, apple2DiskImageOrderAuto) ==
         apple2DiskImageOk);
  assert(image.probe.content == apple2DiskImageContentPascal && image.probe.orderKnown);
  assert(image.probe.order == apple2DiskImageOrderProdos && image.order == apple2DiskImageOrderProdos);
  assert(strcmp(image.probe.volumeName, "BIGVOL") == 0 && image.probe.volumeBlocks == blocks640k);
  assert(!image.orderSuspect);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
  assert(unlink(bigPath) == 0);
  free(big);

  //-- 140K Apple Pascal volume stored in DOS order (.dsk) is recognised by content alone.
  uint8_t *prodos = calloc(apple2DiskImageSize, 1);
  uint8_t *dos = calloc(apple2DiskImageSize, 1);
  assert(prodos != NULL && dos != NULL);
  buildPascalProdosImage(prodos, blocks140k, "SMALL");
  convertProdosToDosOrder(prodos, dos, 35);
  char dosPath[64];
  makeTempPath(dosPath, sizeof(dosPath), ".dsk");
  writeFile(dosPath, dos, apple2DiskImageSize);
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, dosPath, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
         apple2DiskImageOk);
  assert(image.probe.content == apple2DiskImageContentPascal && image.probe.orderKnown);
  assert(image.probe.order == apple2DiskImageOrderDos && image.order == apple2DiskImageOrderDos);
  assert(strcmp(image.probe.volumeName, "SMALL") == 0 && image.probe.volumeBlocks == blocks140k);
  assert(!image.orderSuspect);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);

  //-- A forced wrong order is used as told but reported as suspect.
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, dosPath, apple2DiskImageProfile140k, apple2DiskImageOrderProdos) ==
         apple2DiskImageOk);
  assert(image.order == apple2DiskImageOrderProdos && image.orderSuspect);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
  assert(unlink(dosPath) == 0);

  //-- The ".po" extension decides the order when it is given, even for DOS-order content.
  char poPath[64];
  makeTempPath(poPath, sizeof(poPath), ".po");
  writeFile(poPath, dos, apple2DiskImageSize);
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, poPath, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
         apple2DiskImageOk);
  assert(image.order == apple2DiskImageOrderProdos && image.orderSuspect && image.probe.order == apple2DiskImageOrderDos);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
  assert(unlink(poPath) == 0);

  //-- Header sanity: bad next-block, bad name length and bad block count are all rejected.
  for (int variant = 0; variant < 4; ++variant)
  {
    uint8_t *broken = calloc(apple2DiskImageSize, 1);
    assert(broken != NULL);
    buildPascalProdosImage(broken, blocks140k, "BROKEN");
    uint8_t *header = &broken[2 * pascalBlockBytes];
    if (variant == 0)
    {
      putWord(&header[2], 7);
    }
    else if (variant == 1)
    {
      header[6] = 0;
    }
    else if (variant == 2)
    {
      putWord(&header[0x0E], 0x8000);
    }
    else
    {
      header[7] = 0x07;
    }
    char brokenPath[64];
    makeTempPath(brokenPath, sizeof(brokenPath), ".po");
    writeFile(brokenPath, broken, apple2DiskImageSize);
    apple2DiskImageInitialize(&image);
    assert(apple2DiskImageOpenProfile(&image, brokenPath, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
           apple2DiskImageOk);
    assert(image.probe.content == apple2DiskImageContentUnknown);
    assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
    assert(unlink(brokenPath) == 0);
    free(broken);
  }
  free(prodos);
  free(dos);
}

static void testDos33Probe(void)
{
  for (int variant = 0; variant < 2; ++variant)
  {
    apple2DiskSectorOrder order = variant == 0 ? apple2DiskSectorOrderDos : apple2DiskSectorOrderProdos;
    uint8_t *data = calloc(apple2DiskImageSize, 1);
    assert(data != NULL);
    buildDos33Image(data, order);
    char path[64];
    makeTempPath(path, sizeof(path), ".img");
    writeFile(path, data, apple2DiskImageSize);
    apple2DiskImage image;
    apple2DiskImageInitialize(&image);
    assert(apple2DiskImageOpenProfile(&image, path, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
           apple2DiskImageOk);
    assert(image.probe.content == apple2DiskImageContentDos33 && image.probe.orderKnown);
    assert(image.probe.order == (order == apple2DiskSectorOrderDos ? apple2DiskImageOrderDos : apple2DiskImageOrderProdos));
    assert(image.order == image.probe.order && !image.orderSuspect);
    assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
    assert(unlink(path) == 0);
    free(data);
  }

  //-- A VTOC with a wrong constant is not DOS 3.3.
  uint8_t *data = calloc(apple2DiskImageSize, 1);
  assert(data != NULL);
  buildDos33Image(data, apple2DiskSectorOrderDos);
  data[17 * 16 * sectorSize + 0x27] = 0;
  char path[64];
  makeTempPath(path, sizeof(path), ".dsk");
  writeFile(path, data, apple2DiskImageSize);
  apple2DiskImage image;
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, path, apple2DiskImageProfile140k, apple2DiskImageOrderAuto) ==
         apple2DiskImageOk);
  assert(image.probe.content == apple2DiskImageContentUnknown);
  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
  assert(unlink(path) == 0);
  free(data);

  apple2DiskImageProbeResult result;
  assert(apple2DiskImageProbe(NULL, &result) == apple2DiskImageInvalidArgument);
  assert(apple2DiskImageProbe(&image, &result) == apple2DiskImageNotOpen);
}

//-- A 640K Pascal volume read through the Disk II model: ProDOS block 1000 (track 125) is the physical sector
//-- pair 0 and 2 of that track, and nothing in the image file changes.
static void testPascal640kThroughController(void)
{
  uint8_t *data = calloc(apple2DiskImage640kSize, 1);
  assert(data != NULL);
  buildPascalProdosImage(data, blocks640k, "BIGVOL");
  for (size_t offset = 0; offset < pascalBlockBytes; ++offset)
  {
    data[(size_t)1000 * pascalBlockBytes + offset] = patternByte(9, 0, 0, offset);
    data[(size_t)1279 * pascalBlockBytes + offset] = patternByte(10, 0, 0, offset);
  }
  char path[64];
  makeTempPath(path, sizeof(path), ".po");
  writeFile(path, data, apple2DiskImage640kSize);

  apple2DiskImage image;
  apple2DiskImageInitialize(&image);
  assert(apple2DiskImageOpenProfile(&image, path, apple2DiskImageProfile640k, apple2DiskImageOrderAuto) ==
         apple2DiskImageOk);
  apple2Disk disk;
  apple2DiskInitialize(&disk);
  assert(apple2DiskAttachDrive(&disk, 0, image.order == apple2DiskImageOrderProdos ? apple2DiskSectorOrderProdos
                                                                                   : apple2DiskSectorOrderDos,
                               image.trackCount, apple2DiskImageReadSectorCallback, &image));
  apple2DiskAccess(&disk, 0x9, false, 0, 0);

  static uint8_t stream[apple2DiskNibblesPerTrack];
  uint8_t sectors[16][256];
  stepToHalfTrack(&disk, 2 * 125);
  readTrackStream(&disk, stream);
  assert(decodeTrack(stream, 125, sectors) == 0xFFFFU);
  for (size_t offset = 0; offset < sectorSize; ++offset)
  {
    assert(sectors[0][offset] == data[(size_t)1000 * pascalBlockBytes + offset]);
    assert(sectors[2][offset] == data[(size_t)1000 * pascalBlockBytes + sectorSize + offset]);
  }
  stepToHalfTrack(&disk, 2 * 159);
  readTrackStream(&disk, stream);
  assert(decodeTrack(stream, 159, sectors) == 0xFFFFU);
  //-- Block 1279 is the last block: track 159, block-in-track 7 = physical sectors 13 and 15.
  for (size_t offset = 0; offset < sectorSize; ++offset)
  {
    assert(sectors[13][offset] == data[(size_t)1279 * pascalBlockBytes + offset]);
    assert(sectors[15][offset] == data[(size_t)1279 * pascalBlockBytes + sectorSize + offset]);
  }
  assert(disk.sectorReadFailures == 0 && disk.writeAttempts == 0);

  assert(apple2DiskImageClose(&image) == apple2DiskImageOk);
  size_t afterSize = 0;
  FILE *file = fopen(path, "rb");
  assert(file != NULL);
  uint8_t *after = malloc(apple2DiskImage640kSize);
  assert(after != NULL);
  afterSize = fread(after, 1, apple2DiskImage640kSize, file);
  assert(fclose(file) == 0);
  assert(afterSize == apple2DiskImage640kSize && memcmp(after, data, apple2DiskImage640kSize) == 0);
  free(after);
  assert(unlink(path) == 0);
  free(data);
}

static void writeText(const char *path, const char *text)
{
  writeFile(path, (const uint8_t *)text, strlen(text));
}

static void expectDefaults(apple2DriveConfig drives[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot])
{
  for (size_t slot = 0; slot < apple2DriveConfigSlotCount; ++slot)
  {
    for (size_t drive = 0; drive < apple2DriveConfigDrivesPerSlot; ++drive)
    {
      bool system = slot == 6 - apple2DriveConfigFirstSlot && drive == 0;
      assert(drives[slot][drive].configured == system);
    }
  }
  assert(strcmp(drives[2][0].path, "/littlefs/apple2/system.dsk") == 0);
  assert(drives[2][0].profile == apple2DiskImageProfile140k);
}

static void testDriveConfig(void)
{
  apple2DriveConfig drives[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot];
  char error[128];
  assert(apple2DriveConfigLoad(NULL, drives, error, sizeof(error)) == apple2DriveConfigInvalid);
  assert(apple2DriveConfigLoad("/tmp/apple2MultiDisk-no-such-drives.cfg", drives, error, sizeof(error)) ==
         apple2DriveConfigMissing);
  expectDefaults(drives);

  char path[64];
  makeTempPath(path, sizeof(path), ".cfg");
  writeText(path, "# Apple II drives\n"
                  "\n"
                  "PR6.2=/retro/images/apple2/data.po,RO,APPLE2_640K\n"
                  "PR5.1 = /retro/images/apple2/pascal.dsk , RO , APPLE2_140K\r\n"
                  "PR7.2=/littlefs/apple2/extra.dsk,RO,APPLE2_140K\n");
  assert(apple2DriveConfigLoad(path, drives, error, sizeof(error)) == apple2DriveConfigLoaded);
  assert(drives[2][0].configured && strcmp(drives[2][0].path, "/littlefs/apple2/system.dsk") == 0);
  assert(drives[2][1].configured && drives[2][1].profile == apple2DiskImageProfile640k);
  assert(strcmp(drives[2][1].path, "/microSD/retro/images/apple2/data.po") == 0);
  assert(drives[1][0].configured && drives[1][0].profile == apple2DiskImageProfile140k);
  assert(strcmp(drives[1][0].path, "/microSD/retro/images/apple2/pascal.dsk") == 0);
  assert(drives[3][1].configured && strcmp(drives[3][1].path, "/littlefs/apple2/extra.dsk") == 0);
  assert(!drives[0][0].configured && !drives[0][1].configured && !drives[1][1].configured && !drives[3][0].configured);

  //-- PR6.1 may be replaced by another image.
  writeText(path, "PR6.1=/retro/images/apple2/boot.po,RO,APPLE2_640K\n");
  assert(apple2DriveConfigLoad(path, drives, error, sizeof(error)) == apple2DriveConfigLoaded);
  assert(drives[2][0].configured && drives[2][0].profile == apple2DiskImageProfile640k);
  assert(strcmp(drives[2][0].path, "/microSD/retro/images/apple2/boot.po") == 0);

  static const char *const invalidLines[] = {
      "PR6.2=/retro/images/apple2/data.po,RW,APPLE2_640K\n",
      "PR3.1=/retro/images/apple2/data.po,RO,APPLE2_140K\n",
      "PR8.1=/retro/images/apple2/data.po,RO,APPLE2_140K\n",
      "PR6.3=/retro/images/apple2/data.po,RO,APPLE2_140K\n",
      "PR6.0=/retro/images/apple2/data.po,RO,APPLE2_140K\n",
      "S6D2=/retro/images/apple2/data.po,RO,APPLE2_140K\n",
      "PR6.2=/retro/images/apple2/data.po,RO,APPLE2_800K\n",
      "PR6.2=/retro/images/apple2/data.po,RO\n",
      "PR6.2=/retro/images/apple2/data.po\n",
      "PR6.2=/retro/images/apple2/data.po,RO,APPLE2_140K,extra\n",
      "PR6.2=,RO,APPLE2_140K\n",
      "PR6.2=/retro/images/cpm86/data.dsk,RO,APPLE2_140K\n",
      "PR6.2=/retro/images/apple2/../data.dsk,RO,APPLE2_140K\n",
      "PR6.2=/retro/images/apple2/,RO,APPLE2_140K\n",
      "PR6.2=/littlefs/cpm86/system.dsk,RO,APPLE2_140K\n",
      "PR6.2=/retro/images/apple2/a.dsk,RO,APPLE2_140K\nPR6.2=/retro/images/apple2/b.dsk,RO,APPLE2_140K\n",
      "just text\n",
  };
  for (size_t index = 0; index < sizeof(invalidLines) / sizeof(invalidLines[0]); ++index)
  {
    writeText(path, invalidLines[index]);
    error[0] = '\0';
    assert(apple2DriveConfigLoad(path, drives, error, sizeof(error)) == apple2DriveConfigInvalid);
    assert(strstr(error, "line") != NULL);
    expectDefaults(drives);
  }

  //-- An over-long line is refused instead of being cut.
  char longLine[400];
  memset(longLine, 'x', sizeof(longLine) - 2);
  longLine[sizeof(longLine) - 2] = '\n';
  longLine[sizeof(longLine) - 1] = '\0';
  writeText(path, longLine);
  assert(apple2DriveConfigLoad(path, drives, error, sizeof(error)) == apple2DriveConfigInvalid);
  expectDefaults(drives);
  assert(unlink(path) == 0);
}

int main(void)
{
  buildOrderTables();
  buildDecodeTable();
  testOrderTables();
  testAttachValidation();
  testSectorOrders();
  testExtendedTracks();
  testTwoDrives();
  testEmptySecondDrive();
  testSlotsThroughCore();
  testImageProfileSizes();
  testPascalProbe();
  testDos33Probe();
  testPascal640kThroughController();
  testDriveConfig();
  puts("PASS: multi-drive Disk II, 640K media, ProDOS/Pascal order, probing and drives.cfg");
  return 0;
}
