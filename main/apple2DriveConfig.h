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
  bool readOnly;
  bool smartPort;
  bool configured;
} apple2DriveConfig;

typedef enum
{
  apple2DriveConfigLoaded,
  apple2DriveConfigMissing,
  apple2DriveConfigInvalid
} apple2DriveConfigResult;

//-- Loads drives.cfg lines of the form SD<slot>.<drive> or SP5.<unit> followed by image, mode and profile.
//-- The result is indexed [slot - 4][unit - 1]. A drive without a line is empty: there is no default image, not even for SD6.1.
//-- Images must be below /littlefs/apple2/ or /retro/images/apple2/. /littlefs/apple2/system.dsk must be read-only.
//-- On any error all drives are left empty and error describes the problem.
apple2DriveConfigResult apple2DriveConfigLoad(const char *path,
                                              apple2DriveConfig drives[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot],
                                              char *error, size_t errorCapacity);
