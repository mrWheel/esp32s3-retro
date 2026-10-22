#include "guestFs.h"
#include "guestFsInternal.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

enum
{
  sectorsPerTrack = 16,
  size140k = 143360,
  size640k = 655360,
  size800k = 819200
};

//-- Same tables as main/apple2DiskImage.c.
static const uint8_t physicalOfProdosLogical[sectorsPerTrack] = {0, 2, 4, 6, 8, 10, 12, 14,
                                                                 1, 3, 5, 7, 9, 11, 13, 15};
static const uint8_t dosLogicalOfPhysical[sectorsPerTrack] = {0,  7, 14, 6, 13, 5, 12, 4,
                                                              11, 3, 10, 2, 9,  1, 8,  15};
static const uint8_t prodosLogicalOfPhysical[sectorsPerTrack] = {0, 8,  1, 9,  2, 10, 3, 11,
                                                                 4, 12, 5, 13, 6, 14, 7, 15};

bool guestFsAppleSupportsOs(const char* osId)
{
  return osId != NULL && (strcmp(osId, "apple2") == 0 || strcmp(osId, "ucsd") == 0);
}

bool guestFsJournalExists(const char* journalPath)
{
  FILE* file = fopen(journalPath, "rb");
  if (file == NULL)
  {
    return false;
  }
  fclose(file);
  return true;
}

guestFsStatus guestFsRangeCommit(const guestFsIo* io, const char* journalPath,
                                 const char* journalId, const guestFsRangeChange* changes,
                                 size_t count, bool* needsReopen)
{
  guestFsJournal journal;
  guestFsStatus status;
  if (io == NULL || journalPath == NULL || journalId == NULL || changes == NULL || count == 0 ||
      needsReopen == NULL)
  {
    return guestFsErrInvalidArgument;
  }
  if (*needsReopen || guestFsJournalExists(journalPath))
  {
    return guestFsErrIo;
  }
  status = guestFsJournalBegin(&journal, journalPath, journalId, io->size);
  for (size_t index = 0; status == guestFsOk && index < count; ++index)
  {
    status = guestFsJournalSave(&journal, io, changes[index].offset, changes[index].length);
  }
  if (status == guestFsOk)
  {
    status = guestFsJournalSeal(&journal);
  }
  if (status != guestFsOk)
  {
    guestFsJournalAbandon(&journal);
    if (status == guestFsErrAborted)
    {
      *needsReopen = true;
    }
    else
    {
      remove(journalPath);
    }
    return status;
  }
  if (guestFsCrashPoint("sealed"))
  {
    guestFsJournalAbandon(&journal);
    *needsReopen = true;
    return guestFsErrAborted;
  }
  for (size_t index = 0; status == guestFsOk && index < count; ++index)
  {
    if (!io->writeAt(io->context, changes[index].offset, changes[index].data,
                     changes[index].length))
    {
      status = guestFsErrIo;
    }
    else if (guestFsCrashPoint("slotWritten"))
    {
      guestFsJournalAbandon(&journal);
      *needsReopen = true;
      return guestFsErrAborted;
    }
  }
  if (status == guestFsOk && !io->flush(io->context))
  {
    status = guestFsErrIo;
  }
  if (status == guestFsOk && guestFsCrashPoint("applied"))
  {
    guestFsJournalAbandon(&journal);
    *needsReopen = true;
    return guestFsErrAborted;
  }
  if (status == guestFsOk)
  {
    status = guestFsJournalCommit(&journal);
  }
  if (status == guestFsOk && guestFsCrashPoint("committed"))
  {
    guestFsJournalAbandon(&journal);
    *needsReopen = true;
    return guestFsErrAborted;
  }
  if (status == guestFsOk)
  {
    if (guestFsJournalFinish(&journal) != guestFsOk)
    {
      *needsReopen = true;
    }
    return guestFsOk;
  }
  guestFsJournalAbandon(&journal);
  if (guestFsJournalRecover(journalPath, io) != guestFsOk)
  {
    *needsReopen = true;
  }
  return status;
}

static uint16_t le16(const uint8_t* bytes)
{
  return (uint16_t)(bytes[0] | (bytes[1] << 8));
}

//-- Offset of 256-byte sector `fileSector` (position inside the track of the file) of a track.
static uint64_t fileSectorOffset(uint32_t track, uint32_t fileSector)
{
  return ((uint64_t)track * sectorsPerTrack + fileSector) * guestFsAppleSectorSize;
}

static uint32_t fileSectorOfPhysical(bool dosOrder, uint8_t physical)
{
  return dosOrder ? dosLogicalOfPhysical[physical] : prodosLogicalOfPhysical[physical];
}

//-- Byte offset of one 256-byte half (0 or 1) of a 512-byte block.
static uint64_t blockHalfOffset(const guestFsAppleDevice* device, uint32_t block, uint32_t half)
{
  uint32_t track = block / 8;
  uint8_t logical = (uint8_t)(2 * (block % 8) + half);
  return fileSectorOffset(track,
                          fileSectorOfPhysical(device->dosOrder, physicalOfProdosLogical[logical]));
}

uint32_t guestFsAppleBlockCount(const guestFsAppleDevice* device)
{
  return device->trackCount * 8;
}

bool guestFsAppleReadBlock(const guestFsAppleDevice* device, uint32_t block, void* buffer)
{
  if (block >= guestFsAppleBlockCount(device))
  {
    return false;
  }
  if (!device->dosOrder)
  {
    return device->io.readAt(device->io.context, (uint64_t)block * guestFsAppleBlockSize, buffer,
                             guestFsAppleBlockSize);
  }
  uint8_t* bytes = buffer;
  return device->io.readAt(device->io.context, blockHalfOffset(device, block, 0), bytes,
                           guestFsAppleSectorSize) &&
         device->io.readAt(device->io.context, blockHalfOffset(device, block, 1),
                           bytes + guestFsAppleSectorSize, guestFsAppleSectorSize);
}

bool guestFsAppleWriteBlock(const guestFsAppleDevice* device, uint32_t block, const void* buffer)
{
  if (block >= guestFsAppleBlockCount(device))
  {
    return false;
  }
  if (!device->dosOrder)
  {
    return device->io.writeAt(device->io.context, (uint64_t)block * guestFsAppleBlockSize, buffer,
                              guestFsAppleBlockSize);
  }
  const uint8_t* bytes = buffer;
  return device->io.writeAt(device->io.context, blockHalfOffset(device, block, 0), bytes,
                            guestFsAppleSectorSize) &&
         device->io.writeAt(device->io.context, blockHalfOffset(device, block, 1),
                            bytes + guestFsAppleSectorSize, guestFsAppleSectorSize);
}

size_t guestFsAppleBlockChanges(const guestFsAppleDevice* device, uint32_t block,
                                const uint8_t* data, guestFsRangeChange out[2])
{
  if (!device->dosOrder)
  {
    out[0].offset = (uint64_t)block * guestFsAppleBlockSize;
    out[0].length = guestFsAppleBlockSize;
    out[0].data = data;
    return 1;
  }
  for (uint32_t half = 0; half < 2; ++half)
  {
    out[half].offset = blockHalfOffset(device, block, half);
    out[half].length = guestFsAppleSectorSize;
    out[half].data = data + half * guestFsAppleSectorSize;
  }
  return 2;
}

//-- DOS 3.3 logical sector -> physical sector -> position in the file for the order in use.
static uint64_t dosSectorOffset(const guestFsAppleDevice* device, uint32_t track, uint32_t sector)
{
  uint32_t physical = 0;
  while (physical < sectorsPerTrack && dosLogicalOfPhysical[physical] != sector)
  {
    ++physical;
  }
  return fileSectorOffset(track, fileSectorOfPhysical(device->dosOrder, (uint8_t)physical));
}

bool guestFsAppleReadDosSector(const guestFsAppleDevice* device, uint32_t track, uint32_t sector,
                               void* buffer)
{
  return track < device->trackCount && sector < sectorsPerTrack &&
         device->io.readAt(device->io.context, dosSectorOffset(device, track, sector), buffer,
                           guestFsAppleSectorSize);
}

bool guestFsAppleWriteDosSector(const guestFsAppleDevice* device, uint32_t track, uint32_t sector,
                                const void* buffer)
{
  return track < device->trackCount && sector < sectorsPerTrack &&
         device->io.writeAt(device->io.context, dosSectorOffset(device, track, sector), buffer,
                            guestFsAppleSectorSize);
}

void guestFsAppleDosSectorChange(const guestFsAppleDevice* device, uint32_t track, uint32_t sector,
                                 const uint8_t* data, guestFsRangeChange* out)
{
  out->offset = dosSectorOffset(device, track, sector);
  out->length = guestFsAppleSectorSize;
  out->data = data;
}

//-- Content probes ported from main/apple2DiskImage.c (probePascal / probeProDos).
static bool probeBlock2(const guestFsAppleDevice* device, uint8_t* buffer)
{
  return guestFsAppleReadBlock(device, 2, buffer);
}

static bool probePascal(const guestFsAppleDevice* device)
{
  uint8_t buffer[guestFsAppleBlockSize];
  if (!probeBlock2(device, buffer))
  {
    return false;
  }
  uint8_t nameLength = buffer[6];
  uint16_t blockCount = le16(&buffer[0x0E]);
  if (le16(&buffer[0]) != 0 || le16(&buffer[2]) != 6 || le16(&buffer[4]) != 0 || nameLength < 1 ||
      nameLength > 7 || blockCount < 6 || blockCount > 0x7FFF ||
      le16(&buffer[0x10]) > (guestFsAppleBlockSize * 4 / 26) - 1)
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
  return true;
}

static bool probeProdos(const guestFsAppleDevice* device)
{
  uint8_t buffer[guestFsAppleBlockSize];
  if (!probeBlock2(device, buffer))
  {
    return false;
  }
  uint8_t nameLength = buffer[4] & 0x0F;
  uint16_t blockCount = le16(&buffer[0x29]);
  uint16_t bitmapBlock = le16(&buffer[0x27]);
  if (le16(&buffer[0]) != 0 || le16(&buffer[2]) < 3 ||
      le16(&buffer[2]) >= (uint16_t)(device->trackCount * 8U) || (buffer[4] & 0xF0) != 0xF0 ||
      nameLength < 1 || nameLength > 15 || buffer[0x22] != 0xC3 || buffer[0x23] != 0x27 ||
      buffer[0x24] != 13 || blockCount < 8 || blockCount > (uint16_t)(device->trackCount * 8U) ||
      bitmapBlock < 6 || bitmapBlock >= blockCount)
  {
    return false;
  }
  for (uint8_t index = 0; index < nameLength; ++index)
  {
    uint8_t character = buffer[5 + index];
    if (!((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
          character == '.'))
    {
      return false;
    }
  }
  return true;
}

static bool hasExtension(const char* path, const char* extension)
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

guestFsStatus guestFsAppleOpenDevice(guestFsAppleDevice* device, const guestFsIo* io,
                                     const char* imageId, char* detail, size_t detailCapacity)
{
  if (io->size != size140k && io->size != size640k && io->size != size800k)
  {
    if (detail != NULL && detailCapacity > 0)
    {
      snprintf(detail, detailCapacity,
               "Image size %llu is not a 140K, 640K or 800K Apple II disk image",
               (unsigned long long)io->size);
    }
    return guestFsErrUnsupported;
  }
  memset(device, 0, sizeof(*device));
  device->io = *io;
  device->trackCount = (uint32_t)(io->size / (sectorsPerTrack * guestFsAppleSectorSize));
  //-- Probe both orders like apple2DiskImageProbe: Pascal first, then ProDOS.
  bool orderKnown = false;
  bool probedDosOrder = true;
  for (int content = 0; content < 2 && !orderKnown; ++content)
  {
    bool (*probe)(const guestFsAppleDevice*) = content == 0 ? probePascal : probeProdos;
    device->dosOrder = true;
    bool inDos = probe(device);
    device->dosOrder = false;
    bool inProdos = probe(device);
    if (inDos || inProdos)
    {
      orderKnown = inDos != inProdos;
      probedDosOrder = !inProdos;
      break;
    }
  }
  bool dosOrder = true;
  if (hasExtension(imageId, ".po"))
  {
    dosOrder = false;
  }
  else if (hasExtension(imageId, ".do"))
  {
    dosOrder = true;
  }
  else if (orderKnown)
  {
    dosOrder = probedDosOrder;
  }
  device->dosOrder = dosOrder;
  return guestFsOk;
}
