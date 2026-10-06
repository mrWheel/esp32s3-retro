#pragma once

#include "cpm80Guest.h"
#include <stdbool.h>
#include <stddef.h>

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
} cpm80DriveConfig;

typedef enum
{
  cpm80DriveConfigLoaded,
  cpm80DriveConfigMissing,
  cpm80DriveConfigInvalid
} cpm80DriveConfigResult;

cpm80DriveConfigResult cpm80DriveConfigLoad(const char *path, cpm80DriveConfig drives[cpm80DiskDriveCount], char *error,
                                        size_t errorCapacity);
