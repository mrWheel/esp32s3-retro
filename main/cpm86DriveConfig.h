#pragma once

#include <stdbool.h>
#include <stddef.h>

enum
{
  cpm86DiskDriveCount = 6,
  cpm86DrivePathCapacity = 192
};

typedef enum
{
  cpm86DiskProfileSystem,
  cpm86DiskProfileData
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
