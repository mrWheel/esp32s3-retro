#pragma once

#include "imageFile.h"
#include <stdbool.h>
#include <stdint.h>

enum
{
  apple2DiskImageTracks = 35,
  apple2DiskImageSectorsPerTrack = 16,
  apple2DiskImageSectorSize = 256,
  apple2DiskImageSize = apple2DiskImageTracks * apple2DiskImageSectorsPerTrack * apple2DiskImageSectorSize,
  apple2DiskImage640kTracks = 160,
  apple2DiskImage640kSize = apple2DiskImage640kTracks * apple2DiskImageSectorsPerTrack * apple2DiskImageSectorSize,
  apple2DiskImage800kTracks = 200,
  apple2DiskImage800kSize = apple2DiskImage800kTracks * apple2DiskImageSectorsPerTrack * apple2DiskImageSectorSize,
  apple2DiskImageVolumeNameCapacity = 16
};

//-- Geometry of an image. 640K is 1280 512-byte blocks laid out as 160 tracks of 16 sectors; 800K is 1600
//-- 512-byte blocks (the size of a 3.5-inch ProDOS volume) laid out as 200 tracks of 16 sectors.
typedef enum
{
  apple2DiskImageProfile140k,
  apple2DiskImageProfile640k,
  apple2DiskImageProfile800k
} apple2DiskImageProfile;

//-- Order of the 16 sectors of a track inside the image file. Auto picks it from the file extension
//-- (.po = Prodos, .do = Dos) or else from the content, and falls back to Dos.
typedef enum
{
  apple2DiskImageOrderDos,
  apple2DiskImageOrderProdos,
  apple2DiskImageOrderAuto
} apple2DiskImageOrder;

typedef enum
{
  apple2DiskImageContentUnknown,
  apple2DiskImageContentPascal,
  apple2DiskImageContentProDos
} apple2DiskImageContent;

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
  apple2DiskImageWriteFailed,
  apple2DiskImageNotOpen
} apple2DiskImageResult;

typedef struct
{
  apple2DiskImageContent content;
  //-- True when exactly one sector order produced a valid ProDOS or Apple Pascal layout; order is then that order.
  bool orderKnown;
  apple2DiskImageOrder order;
  //-- Pascal/ProDOS volume name (NUL terminated) and block count; empty/0 for other content.
  char volumeName[apple2DiskImageVolumeNameCapacity];
  uint16_t volumeBlocks;
} apple2DiskImageProbeResult;

typedef struct
{
  imageFile image;
  uint8_t trackCount;
  //-- Sector order used by the drive: apple2DiskImageOrderDos or apple2DiskImageOrderProdos (never Auto).
  apple2DiskImageOrder order;
  apple2DiskImageProbeResult probe;
  //-- True when the chosen order disagrees with the order the content probe found.
  bool orderSuspect;
} apple2DiskImage;

//-- 16-sector Apple II disk image. apple2DiskImageOpen and apple2DiskImageOpenProfile open it read-only; the Mode
//-- variant selects RO/RW. Only one sector is held in memory.
void apple2DiskImageInitialize(apple2DiskImage *disk);
apple2DiskImageResult apple2DiskImageOpen(apple2DiskImage *disk, const char *path);
//-- Opens an image with the geometry of profile. The image size must match the profile exactly.
apple2DiskImageResult apple2DiskImageOpenProfile(apple2DiskImage *disk, const char *path,
                                                 apple2DiskImageProfile profile, apple2DiskImageOrder order);
apple2DiskImageResult apple2DiskImageOpenProfileMode(apple2DiskImage *disk, const char *path,
                                                     apple2DiskImageProfile profile, apple2DiskImageOrder order,
                                                     bool readOnly);
uint8_t apple2DiskImageProfileTracks(apple2DiskImageProfile profile);
//-- Looks for a valid Pascal volume header or ProDOS volume directory under both sector orders.
apple2DiskImageResult apple2DiskImageProbe(apple2DiskImage *disk, apple2DiskImageProbeResult *result);
//-- Sector is the position (0..15) inside the track in the image file, as passed by the Disk II model.
apple2DiskImageResult apple2DiskImageReadSector(apple2DiskImage *disk, uint8_t track, uint8_t sector,
                                                uint8_t *buffer);
apple2DiskImageResult apple2DiskImageWriteSector(apple2DiskImage *disk, uint8_t track, uint8_t sector,
                                                 const uint8_t *buffer);
bool apple2DiskImageReadSectorCallback(void *context, uint8_t track, uint8_t sector, uint8_t *buffer);
bool apple2DiskImageWriteSectorCallback(void *context, uint8_t track, uint8_t sector, const uint8_t *buffer);
apple2DiskImageResult apple2DiskImageReadBlock(apple2DiskImage *disk, uint32_t block, uint8_t *buffer);
apple2DiskImageResult apple2DiskImageWriteBlock(apple2DiskImage *disk, uint32_t block, const uint8_t *buffer);
bool apple2DiskImageReadBlockCallback(void *context, uint32_t block, uint8_t *buffer);
bool apple2DiskImageWriteBlockCallback(void *context, uint32_t block, const uint8_t *buffer);
apple2DiskImageResult apple2DiskImageClose(apple2DiskImage *disk);
bool apple2DiskImageIsOpen(const apple2DiskImage *disk);
const char *apple2DiskImageResultText(apple2DiskImageResult result);
