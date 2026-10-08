#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
  cpm86DiskDriveCount = 6,
  cpm86DrivePathCapacity = 192,
  cpm86SystemDiskSize = 163840,
  cpm86SystemDiskTracks = 40,
  cpm86LargeDiskTracks = 129,
  cpm86LargeDiskSize = 528384,
  cpm86BigDiskTracks = 2049,
  cpm86BigDiskSize = 8392704
};

typedef enum
{
  cpm86DiskProfileSystem,
  cpm86DiskProfileSystemLarge,
  cpm86DiskProfileData,
  cpm86DiskProfileDataLarge,
  cpm86DiskProfileDataBig
} cpm86DiskProfile;

typedef struct
{
  char path[cpm86DrivePathCapacity];
  cpm86DiskProfile profile;
  bool readOnly;
  bool configured;
} cpm86DriveConfig;

typedef enum
{
  cpm86DriveConfigLoaded,
  cpm86DriveConfigMissing,
  cpm86DriveConfigInvalid
} cpm86DriveConfigResult;

cpm86DriveConfigResult cpm86DriveConfigLoad(const char *path,
                                            cpm86DriveConfig drives[cpm86DiskDriveCount],
                                            char *error, size_t errorCapacity);

//-- Maps the size of the A: image to its profile (163840 = System, 528384 = SystemLarge); false for any other size.
bool cpm86DriveConfigSystemProfileFromSize(uint64_t imageSize, cpm86DiskProfile *profile);

//-- Sets drive->profile for A: from imageSize.
bool cpm86DriveConfigResolveSystemProfile(cpm86DriveConfig *drive, uint64_t imageSize, char *error,
                                          size_t errorCapacity);
