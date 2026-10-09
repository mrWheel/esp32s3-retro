#include "apple2DiskImage.h"
#include <string.h>

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
  if (track >= apple2DiskImageTracks || sector >= apple2DiskImageSectorsPerTrack)
  {
    return apple2DiskImageOutOfRange;
  }
  uint64_t offset = ((uint64_t)track * apple2DiskImageSectorsPerTrack + sector) * apple2DiskImageSectorSize;
  return imageReadAt(&disk->image, offset, buffer, apple2DiskImageSectorSize) ? apple2DiskImageOk
                                                                               : apple2DiskImageReadFailed;
}

bool apple2DiskImageReadSectorCallback(void *context, uint8_t track, uint8_t sector, uint8_t *buffer)
{
  return apple2DiskImageReadSector((apple2DiskImage *)context, track, sector, buffer) == apple2DiskImageOk;
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
  return imageClose(&disk->image) ? apple2DiskImageOk : apple2DiskImageReadFailed;
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
    return "image size is not 143360 bytes (35 tracks x 16 sectors x 256 bytes)";
  case apple2DiskImageOpenFailed:
    return "image could not be opened";
  case apple2DiskImageOutOfRange:
    return "track or sector out of range";
  case apple2DiskImageReadFailed:
    return "image read failed";
  case apple2DiskImageNotOpen:
    return "image not open";
  }
  return "unknown result";
}
