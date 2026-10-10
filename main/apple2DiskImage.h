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
  apple2DiskImageVolumeNameCapacity = 8
};

//-- Geometry of an image. 640K is 1280 Apple Pascal blocks of 512 bytes laid out as 160 tracks of 16 sectors.
typedef enum
{
  apple2DiskImageProfile140k,
  apple2DiskImageProfile640k
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
  apple2DiskImageContentDos33,
  apple2DiskImageContentPascal
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
  apple2DiskImageNotOpen
} apple2DiskImageResult;

typedef struct
{
  apple2DiskImageContent content;
  //-- True when exactly one sector order produced a valid DOS 3.3 or Apple Pascal layout; order is then that order.
  bool orderKnown;
  apple2DiskImageOrder order;
  //-- Apple Pascal volume name (NUL terminated) and block count; empty/0 for other content.
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

//-- Read-only 16-sector Apple II disk image. apple2DiskImageOpen is the 35-track DOS-order .dsk image; only one
//-- sector is ever held in memory.
void apple2DiskImageInitialize(apple2DiskImage *disk);
apple2DiskImageResult apple2DiskImageOpen(apple2DiskImage *disk, const char *path);
//-- Opens an image with the geometry of profile. The image size must match the profile exactly.
apple2DiskImageResult apple2DiskImageOpenProfile(apple2DiskImage *disk, const char *path,
                                                 apple2DiskImageProfile profile, apple2DiskImageOrder order);
uint8_t apple2DiskImageProfileTracks(apple2DiskImageProfile profile);
//-- Looks for a valid DOS 3.3 VTOC/catalog or Apple Pascal volume header under both sector orders.
apple2DiskImageResult apple2DiskImageProbe(apple2DiskImage *disk, apple2DiskImageProbeResult *result);
//-- Sector is the position (0..15) inside the track in the image file, as passed by the Disk II model.
apple2DiskImageResult apple2DiskImageReadSector(apple2DiskImage *disk, uint8_t track, uint8_t sector,
                                                uint8_t *buffer);
bool apple2DiskImageReadSectorCallback(void *context, uint8_t track, uint8_t sector, uint8_t *buffer);
apple2DiskImageResult apple2DiskImageClose(apple2DiskImage *disk);
bool apple2DiskImageIsOpen(const apple2DiskImage *disk);
const char *apple2DiskImageResultText(apple2DiskImageResult result);
