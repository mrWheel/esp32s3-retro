#pragma once

#include "cpmGuest.h"
#include <stdbool.h>
#include <stddef.h>

enum
{
  cpmDrivePathCapacity = 192
};

typedef struct
{
  char path[cpmDrivePathCapacity];
  cpmDiskProfile profile;
  bool readOnly;
  bool configured;
} cpmDriveConfig;

typedef enum
{
  cpmDriveConfigLoaded,
  cpmDriveConfigMissing,
  cpmDriveConfigInvalid
} cpmDriveConfigResult;

cpmDriveConfigResult cpmDriveConfigLoad(const char *path, cpmDriveConfig drives[cpmDiskDriveCount], char *error,
                                        size_t errorCapacity);
