#pragma once

#include "apple2DiskImage.h"
#include <stdbool.h>
#include <stddef.h>

enum
{
  apple2DriveConfigFirstSlot = 4,
  apple2DriveConfigLastSlot = 7,
  apple2DriveConfigSlotCount = apple2DriveConfigLastSlot - apple2DriveConfigFirstSlot + 1,
  apple2DriveConfigDrivesPerSlot = 2,
  apple2DriveConfigPathCapacity = 192
};

typedef struct
{
  char path[apple2DriveConfigPathCapacity];
  apple2DiskImageProfile profile;
  bool configured;
} apple2DriveConfig;

typedef enum
{
  apple2DriveConfigLoaded,
  apple2DriveConfigMissing,
  apple2DriveConfigInvalid
} apple2DriveConfigResult;

//-- Loads drives.cfg lines of the form PR<slot>.<drive>=<image>,RO,<APPLE2_140K|APPLE2_640K> (slot 4..7, drive 1..2).
//-- The result is indexed [slot - 4][drive - 1]. Without a line for PR6.1 that drive is the built-in system image.
//-- Images must be below /littlefs/apple2/ or /retro/images/apple2/. Only read-only (RO) drives are supported.
//-- On any error all drives fall back to the defaults and error describes the problem.
apple2DriveConfigResult apple2DriveConfigLoad(const char *path,
                                              apple2DriveConfig drives[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot],
                                              char *error, size_t errorCapacity);
