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
  apple2DriveConfigSmartPortUnits = 4,
  apple2DriveConfigDevicesPerSlot = apple2DriveConfigSmartPortUnits,
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

//-- Loads drives.cfg lines of the form SD<slot>.<drive> or SP<slot>.<unit> followed by image, mode
// and profile.
//-- The result is indexed [slot - 4][unit - 1]. SD drives support units 1..2; SmartPort supports
// units 1..4.
//-- A drive without a line is empty: there is no default image, not even for SD6.1.
//-- Images may be below /littlefs/apple2/, /littlefs/ucsd/, /retro/images/apple2/ or
/// retro/images/ucsd/.
//-- LittleFS images may be read/write when configured; for UCSD, only the SD6.1 boot image may be
// writable.
//-- On any error all drives are left empty and error describes the problem.
apple2DriveConfigResult apple2DriveConfigLoad(
    const char* path,
    apple2DriveConfig drives[apple2DriveConfigSlotCount][apple2DriveConfigDevicesPerSlot],
    char* error, size_t errorCapacity);

//-- Returns true when drives.cfg contains "RAMCARD=SATURN128" (a Saturn 128K card in slot 0). The
// default, also
//-- selected by "RAMCARD=LC16" or a missing line, is the 16 KiB Language Card. Call after
// apple2DriveConfigLoad validated the file.
bool apple2DriveConfigRamCardIsSaturn128(const char* path);

//-- Returns true when drives.cfg contains "MACHINE=IIE": an Apple IIe with 128 KiB (main +
// auxiliary) memory. The
//-- default, also selected by "MACHINE=II" or a missing line, is the Apple II Plus. RAMCARD is
// ignored on a IIe.
bool apple2DriveConfigMachineIsIIe(const char* path);
