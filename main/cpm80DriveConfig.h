#pragma once

#include "cpm80Guest.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
  cpm80DrivePathCapacity = 192
};

typedef struct
{
  char path[cpm80DrivePathCapacity];
  cpm80DiskProfile profile;
  bool readOnly;
  bool configured;
  bool profileFromSize;
} cpm80DriveConfig;

typedef enum
{
  cpm80DriveConfigLoaded,
  cpm80DriveConfigMissing,
  cpm80DriveConfigInvalid
} cpm80DriveConfigResult;

cpm80DriveConfigResult cpm80DriveConfigLoad(const char *path, cpm80DriveConfig drives[cpm80DiskDriveCount], char *error,
                                        size_t errorCapacity);

//-- Maps the size of the A: image to its profile (256256 = SYSTEM, 512512 = LARGE); false for any other size.
bool cpm80DriveConfigSystemProfileFromSize(uint64_t imageSize, cpm80DiskProfile *profile);

//-- Sets drive->profile for A: from imageSize. An explicit LARGE in drives.cfg must match the file size.
bool cpm80DriveConfigResolveSystemProfile(cpm80DriveConfig *drive, uint64_t imageSize, char *error,
                                          size_t errorCapacity);
