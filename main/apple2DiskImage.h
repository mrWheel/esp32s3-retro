#pragma once

#include "imageFile.h"
#include <stdbool.h>
#include <stdint.h>

enum
{
  apple2DiskImageTracks = 35,
  apple2DiskImageSectorsPerTrack = 16,
  apple2DiskImageSectorSize = 256,
  apple2DiskImageSize = apple2DiskImageTracks * apple2DiskImageSectorsPerTrack * apple2DiskImageSectorSize
};

typedef enum
{
  apple2DiskImageOk,
  apple2DiskImageInvalidArgument,
  apple2DiskImageAlreadyOpen,
  apple2DiskImageNotFound,
  apple2DiskImageBadSize,
  apple2DiskImageOpenFailed,
  apple2DiskImageOutOfRange,
  apple2DiskImageReadFailed,
  apple2DiskImageNotOpen
} apple2DiskImageResult;

typedef struct
{
  imageFile image;
} apple2DiskImage;

//-- Read-only DOS 3.3 (16-sector, 35-track, DOS-order) .dsk image. Only one sector is ever held in memory.
void apple2DiskImageInitialize(apple2DiskImage *disk);
apple2DiskImageResult apple2DiskImageOpen(apple2DiskImage *disk, const char *path);
apple2DiskImageResult apple2DiskImageReadSector(apple2DiskImage *disk, uint8_t track, uint8_t sector,
                                                uint8_t *buffer);
bool apple2DiskImageReadSectorCallback(void *context, uint8_t track, uint8_t sector, uint8_t *buffer);
apple2DiskImageResult apple2DiskImageClose(apple2DiskImage *disk);
bool apple2DiskImageIsOpen(const apple2DiskImage *disk);
const char *apple2DiskImageResultText(apple2DiskImageResult result);
