#include "apple2DiskImage.h"
#include <stdio.h>
#include <string.h>

//-- ProDOS/Pascal logical sector to physical sector.
static const uint8_t physicalOfProdosLogical[apple2DiskImageSectorsPerTrack] = {0, 2, 4, 6, 8, 10, 12, 14,
                                                                                1, 3, 5, 7, 9, 11, 13, 15};
//-- Physical sector to the sector position inside an image file, per file order.
static const uint8_t dosLogicalOfPhysical[apple2DiskImageSectorsPerTrack] = {0, 7, 14, 6, 13, 5, 12, 4,
                                                                             11, 3, 10, 2, 9, 1, 8, 15};
static const uint8_t prodosLogicalOfPhysical[apple2DiskImageSectorsPerTrack] = {0, 8, 1, 9, 2, 10, 3, 11,
                                                                                4, 12, 5, 13, 6, 14, 7, 15};

enum
{
  pascalBlockSize = 512,
  pascalDirectoryBlock = 2,
  pascalDirectoryEntrySize = 26,
  prodosDirectoryNameOffset = 4,
  prodosDirectoryTypeOffset = 0x22,
  prodosDirectoryEntryLengthOffset = 0x23,
  prodosDirectoryEntriesPerBlockOffset = 0x24,
  prodosDirectoryBitmapOffset = 0x27,
  prodosDirectoryBlockCountOffset = 0x29,
  dosSectorSize = 256
};

void apple2DiskImageInitialize(apple2DiskImage *disk)
{
  if (disk != NULL)
  {
    memset(disk, 0, sizeof(*disk));
  }
}

apple2DiskImageResult apple2DiskImageOpen(apple2DiskImage *disk, const char *path)
{
  if (disk == NULL || path == NULL)
  {
    return apple2DiskImageInvalidArgument;
  }
  if (disk->image.file != NULL)
  {
    return apple2DiskImageAlreadyOpen;
  }
  uint64_t size;
  if (!resourceSize(path, &size))
  {
    return apple2DiskImageNotFound;
  }
  if (size != apple2DiskImageSize)
  {
    return apple2DiskImageBadSize;
  }
  if (!imageOpen(&disk->image, path, true))
  {
    disk->image.size = 0;
    return apple2DiskImageOpenFailed;
  }
  disk->trackCount = apple2DiskImageTracks;
  disk->order = apple2DiskImageOrderDos;
  return apple2DiskImageOk;
}

uint8_t apple2DiskImageProfileTracks(apple2DiskImageProfile profile)
{
  if (profile == apple2DiskImageProfile800k)
  {
    return apple2DiskImage800kTracks;
  }
  return profile == apple2DiskImageProfile640k ? apple2DiskImage640kTracks : apple2DiskImageTracks;
}

static bool hasExtension(const char *path, const char *extension)
{
  size_t pathLength = strlen(path);
  size_t extensionLength = strlen(extension);
  if (pathLength <= extensionLength)
  {
    return false;
  }
  for (size_t index = 0; index < extensionLength; ++index)
  {
    char character = path[pathLength - extensionLength + index];
    if (character >= 'A' && character <= 'Z')
    {
      character = (char)(character - 'A' + 'a');
    }
    if (character != extension[index])
    {
      return false;
    }
  }
  return true;
}

//-- Reads one 256-byte sector addressed by a 0..15 position inside the track of the file.
static bool readFileSector(apple2DiskImage *disk, uint8_t track, uint8_t fileSector, uint8_t *buffer)
{
  return track < disk->trackCount && fileSector < apple2DiskImageSectorsPerTrack &&
         imageReadAt(&disk->image, ((uint64_t)track * apple2DiskImageSectorsPerTrack + fileSector) * dosSectorSize,
                     buffer, dosSectorSize);
}

static uint8_t fileSectorOfPhysical(apple2DiskImageOrder order, uint8_t physical)
{
  return order == apple2DiskImageOrderProdos ? prodosLogicalOfPhysical[physical] : dosLogicalOfPhysical[physical];
}

//-- Reads one 512-byte ProDOS/Pascal block (two ProDOS logical sectors) as seen by a file stored in the given order.
static bool readPascalBlock(apple2DiskImage *disk, apple2DiskImageOrder order, uint32_t block, uint8_t *buffer)
{
  uint32_t track = block / 8;
  if (track >= disk->trackCount)
  {
    return false;
  }
  for (uint8_t half = 0; half < 2; ++half)
  {
    uint8_t logical = (uint8_t)(2 * (block % 8) + half);
    if (!readFileSector(disk, (uint8_t)track, fileSectorOfPhysical(order, physicalOfProdosLogical[logical]),
                        &buffer[half * dosSectorSize]))
    {
      return false;
    }
  }
  return true;
}

static uint16_t littleEndian16(const uint8_t *bytes)
{
  return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

//-- Apple Pascal volume header: directory block 2 holds a 26-byte entry with first block 0, next block 6,
//-- file kind 0, a 1..7 character volume name, the block count and the file count.
static bool probePascal(apple2DiskImage *disk, apple2DiskImageOrder order, char *name, uint16_t *blocks)
{
  uint8_t buffer[pascalBlockSize];
  if (!readPascalBlock(disk, order, pascalDirectoryBlock, buffer))
  {
    return false;
  }
  uint8_t nameLength = buffer[6];
  uint16_t blockCount = littleEndian16(&buffer[0x0E]);
  if (littleEndian16(&buffer[0]) != 0 || littleEndian16(&buffer[2]) != 6 || littleEndian16(&buffer[4]) != 0 ||
      nameLength < 1 || nameLength > 7 || blockCount < 6 || blockCount > 0x7FFF ||
      littleEndian16(&buffer[0x10]) > (pascalBlockSize * 4 / pascalDirectoryEntrySize) - 1)
  {
    return false;
  }
  for (uint8_t index = 0; index < nameLength; ++index)
  {
    if (buffer[7 + index] <= 0x20 || buffer[7 + index] >= 0x7F)
    {
      return false;
    }
  }
  memcpy(name, &buffer[7], nameLength);
  name[nameLength] = '\0';
  *blocks = blockCount;
  return true;
}

//-- ProDOS volume header entry in directory block 2; all byte offsets are relative to the start of that block.
static bool probeProDos(apple2DiskImage *disk, apple2DiskImageOrder order, char *name, uint16_t *blocks)
{
  uint8_t buffer[pascalBlockSize];
  if (!readPascalBlock(disk, order, pascalDirectoryBlock, buffer))
  {
    return false;
  }
  uint8_t nameLength = buffer[prodosDirectoryNameOffset] & 0x0FU;
  uint16_t blockCount = littleEndian16(&buffer[prodosDirectoryBlockCountOffset]);
  uint16_t bitmapBlock = littleEndian16(&buffer[prodosDirectoryBitmapOffset]);
  if (littleEndian16(&buffer[0]) != 0 || littleEndian16(&buffer[2]) < 3 ||
      littleEndian16(&buffer[2]) >= (uint16_t)disk->trackCount * 8U ||
      (buffer[prodosDirectoryNameOffset] & 0xF0U) != 0xF0U || nameLength < 1 || nameLength > 15 ||
      buffer[prodosDirectoryTypeOffset] != 0xC3 ||
      buffer[prodosDirectoryEntryLengthOffset] != 0x27 ||
      buffer[prodosDirectoryEntriesPerBlockOffset] != 13 || blockCount < 8 ||
      blockCount > (uint16_t)disk->trackCount * 8U || bitmapBlock < 6 || bitmapBlock >= blockCount)
  {
    return false;
  }
  for (uint8_t index = 0; index < nameLength; ++index)
  {
    uint8_t character = buffer[prodosDirectoryNameOffset + 1 + index];
    if (!((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '.'))
    {
      return false;
    }
  }
  memcpy(name, &buffer[prodosDirectoryNameOffset + 1], nameLength);
  name[nameLength] = '\0';
  *blocks = blockCount;
  return true;
}

apple2DiskImageResult apple2DiskImageProbe(apple2DiskImage *disk, apple2DiskImageProbeResult *result)
{
  if (disk == NULL || result == NULL)
  {
    return apple2DiskImageInvalidArgument;
  }
  if (disk->image.file == NULL)
  {
    return apple2DiskImageNotOpen;
  }
  memset(result, 0, sizeof(*result));
  result->content = apple2DiskImageContentUnknown;

  char dosName[apple2DiskImageVolumeNameCapacity];
  char prodosName[apple2DiskImageVolumeNameCapacity];
  uint16_t dosBlocks = 0;
  uint16_t prodosBlocks = 0;
  bool pascalDos = probePascal(disk, apple2DiskImageOrderDos, dosName, &dosBlocks);
  bool pascalProdos = probePascal(disk, apple2DiskImageOrderProdos, prodosName, &prodosBlocks);
  if (pascalDos || pascalProdos)
  {
    result->content = apple2DiskImageContentPascal;
    result->orderKnown = pascalDos != pascalProdos;
    result->order = pascalProdos ? apple2DiskImageOrderProdos : apple2DiskImageOrderDos;
    snprintf(result->volumeName, sizeof(result->volumeName), "%s", pascalProdos ? prodosName : dosName);
    result->volumeBlocks = pascalProdos ? prodosBlocks : dosBlocks;
    return apple2DiskImageOk;
  }

  bool proDosDos = probeProDos(disk, apple2DiskImageOrderDos, dosName, &dosBlocks);
  bool proDosProdos = probeProDos(disk, apple2DiskImageOrderProdos, prodosName, &prodosBlocks);
  if (proDosDos || proDosProdos)
  {
    result->content = apple2DiskImageContentProDos;
    result->orderKnown = proDosDos != proDosProdos;
    result->order = proDosProdos ? apple2DiskImageOrderProdos : apple2DiskImageOrderDos;
    snprintf(result->volumeName, sizeof(result->volumeName), "%s", proDosProdos ? prodosName : dosName);
    result->volumeBlocks = proDosProdos ? prodosBlocks : dosBlocks;
    return apple2DiskImageOk;
  }

  return apple2DiskImageOk;
}

apple2DiskImageResult apple2DiskImageOpenProfile(apple2DiskImage *disk, const char *path,
                                                 apple2DiskImageProfile profile, apple2DiskImageOrder order)
{
  return apple2DiskImageOpenProfileMode(disk, path, profile, order, true);
}

apple2DiskImageResult apple2DiskImageOpenProfileMode(apple2DiskImage *disk, const char *path,
                                                     apple2DiskImageProfile profile, apple2DiskImageOrder order,
                                                     bool readOnly)
{
  if (disk == NULL || path == NULL || (profile != apple2DiskImageProfile140k && profile != apple2DiskImageProfile640k &&
       profile != apple2DiskImageProfile800k) ||
      (order != apple2DiskImageOrderDos && order != apple2DiskImageOrderProdos && order != apple2DiskImageOrderAuto))
  {
    return apple2DiskImageInvalidArgument;
  }
  if (disk->image.file != NULL)
  {
    return apple2DiskImageAlreadyOpen;
  }
  uint64_t size;
  if (!resourceSize(path, &size))
  {
    return apple2DiskImageNotFound;
  }
  uint8_t tracks = apple2DiskImageProfileTracks(profile);
  if (size != (uint64_t)tracks * apple2DiskImageSectorsPerTrack * apple2DiskImageSectorSize)
  {
    return apple2DiskImageBadSize;
  }
  if (!imageOpen(&disk->image, path, readOnly))
  {
    disk->image.size = 0;
    return apple2DiskImageOpenFailed;
  }
  disk->trackCount = tracks;
  disk->order = apple2DiskImageOrderDos;
  disk->orderSuspect = false;
  if (apple2DiskImageProbe(disk, &disk->probe) != apple2DiskImageOk)
  {
    memset(&disk->probe, 0, sizeof(disk->probe));
  }

  if (order == apple2DiskImageOrderAuto)
  {
    if (hasExtension(path, ".po"))
    {
      order = apple2DiskImageOrderProdos;
    }
    else if (hasExtension(path, ".do"))
    {
      order = apple2DiskImageOrderDos;
    }
    else
    {
      order = disk->probe.orderKnown ? disk->probe.order : apple2DiskImageOrderDos;
    }
  }
  disk->order = order;
  disk->orderSuspect = disk->probe.orderKnown && disk->probe.order != order;
  return apple2DiskImageOk;
}

apple2DiskImageResult apple2DiskImageReadSector(apple2DiskImage *disk, uint8_t track, uint8_t sector,
                                                uint8_t *buffer)
{
  if (disk == NULL || buffer == NULL)
  {
    return apple2DiskImageInvalidArgument;
  }
  if (disk->image.file == NULL)
  {
    return apple2DiskImageNotOpen;
  }
  if (track >= disk->trackCount || sector >= apple2DiskImageSectorsPerTrack)
  {
    return apple2DiskImageOutOfRange;
  }
  uint64_t offset = ((uint64_t)track * apple2DiskImageSectorsPerTrack + sector) * apple2DiskImageSectorSize;
  return imageReadAt(&disk->image, offset, buffer, apple2DiskImageSectorSize) ? apple2DiskImageOk
                                                                               : apple2DiskImageReadFailed;
}

apple2DiskImageResult apple2DiskImageWriteSector(apple2DiskImage *disk, uint8_t track, uint8_t sector,
                                                 const uint8_t *buffer)
{
  if (disk == NULL || buffer == NULL)
  {
    return apple2DiskImageInvalidArgument;
  }
  if (disk->image.file == NULL)
  {
    return apple2DiskImageNotOpen;
  }
  if (track >= disk->trackCount || sector >= apple2DiskImageSectorsPerTrack)
  {
    return apple2DiskImageOutOfRange;
  }
  uint64_t offset = ((uint64_t)track * apple2DiskImageSectorsPerTrack + sector) * apple2DiskImageSectorSize;
  if (!imageWriteAt(&disk->image, offset, buffer, apple2DiskImageSectorSize) || !imageFlush(&disk->image))
  {
    return apple2DiskImageWriteFailed;
  }
  return apple2DiskImageOk;
}

apple2DiskImageResult apple2DiskImageReadBlock(apple2DiskImage *disk, uint32_t block, uint8_t *buffer)
{
  if (disk == NULL || buffer == NULL)
  {
    return apple2DiskImageInvalidArgument;
  }
  if (disk->image.file == NULL)
  {
    return apple2DiskImageNotOpen;
  }
  if (block >= (uint32_t)disk->trackCount * 8U)
  {
    return apple2DiskImageOutOfRange;
  }
  return readPascalBlock(disk, disk->order, block, buffer) ? apple2DiskImageOk : apple2DiskImageReadFailed;
}

apple2DiskImageResult apple2DiskImageWriteBlock(apple2DiskImage *disk, uint32_t block, const uint8_t *buffer)
{
  if (disk == NULL || buffer == NULL)
  {
    return apple2DiskImageInvalidArgument;
  }
  if (disk->image.file == NULL)
  {
    return apple2DiskImageNotOpen;
  }
  if (block >= (uint32_t)disk->trackCount * 8U)
  {
    return apple2DiskImageOutOfRange;
  }
  uint32_t track = block / 8U;
  for (uint8_t half = 0; half < 2; ++half)
  {
    uint8_t logical = (uint8_t)(2U * (block % 8U) + half);
    uint8_t fileSector = fileSectorOfPhysical(disk->order, physicalOfProdosLogical[logical]);
    uint64_t offset = ((uint64_t)track * apple2DiskImageSectorsPerTrack + fileSector) * dosSectorSize;
    if (!imageWriteAt(&disk->image, offset, &buffer[half * dosSectorSize], dosSectorSize))
    {
      return apple2DiskImageWriteFailed;
    }
  }
  return imageFlush(&disk->image) ? apple2DiskImageOk : apple2DiskImageWriteFailed;
}

bool apple2DiskImageReadSectorCallback(void *context, uint8_t track, uint8_t sector, uint8_t *buffer)
{
  return apple2DiskImageReadSector((apple2DiskImage *)context, track, sector, buffer) == apple2DiskImageOk;
}

bool apple2DiskImageWriteSectorCallback(void *context, uint8_t track, uint8_t sector, const uint8_t *buffer)
{
  return apple2DiskImageWriteSector((apple2DiskImage *)context, track, sector, buffer) == apple2DiskImageOk;
}

bool apple2DiskImageReadBlockCallback(void *context, uint32_t block, uint8_t *buffer)
{
  return apple2DiskImageReadBlock((apple2DiskImage *)context, block, buffer) == apple2DiskImageOk;
}

bool apple2DiskImageWriteBlockCallback(void *context, uint32_t block, const uint8_t *buffer)
{
  return apple2DiskImageWriteBlock((apple2DiskImage *)context, block, buffer) == apple2DiskImageOk;
}

apple2DiskImageResult apple2DiskImageClose(apple2DiskImage *disk)
{
  if (disk == NULL)
  {
    return apple2DiskImageInvalidArgument;
  }
  if (disk->image.file == NULL)
  {
    return apple2DiskImageNotOpen;
  }
  bool readOnly = disk->image.readOnly;
  return imageClose(&disk->image) ? apple2DiskImageOk
                                  : (readOnly ? apple2DiskImageReadFailed : apple2DiskImageWriteFailed);
}

bool apple2DiskImageIsOpen(const apple2DiskImage *disk)
{
  return disk != NULL && disk->image.file != NULL;
}

const char *apple2DiskImageResultText(apple2DiskImageResult result)
{
  switch (result)
  {
  case apple2DiskImageOk:
    return "ok";
  case apple2DiskImageInvalidArgument:
    return "invalid argument";
  case apple2DiskImageAlreadyOpen:
    return "image already open";
  case apple2DiskImageNotFound:
    return "image missing or not a regular file";
  case apple2DiskImageBadSize:
    return "image size does not match the drive profile (140K = 143360 bytes, 640K = 655360 bytes, 800K = 819200 bytes)";
  case apple2DiskImageOpenFailed:
    return "image could not be opened";
  case apple2DiskImageOutOfRange:
    return "track or sector out of range";
  case apple2DiskImageReadFailed:
    return "image read failed";
  case apple2DiskImageWriteFailed:
    return "image write failed";
  case apple2DiskImageNotOpen:
    return "image not open";
  }
  return "unknown result";
}
