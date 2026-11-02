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

//-- The three image sizes have drives.cfg identifiers CPM86_160K (163840 bytes),
//-- CPM86_516K (528384 bytes) and CPM86_8196K (8392704 bytes). System* profiles are the
//-- built-in A: boot image, Data* profiles the images on the SD card; both share a geometry.
typedef enum
{
  cpm86DiskProfileSystem,
  cpm86DiskProfileSystemLarge,
  cpm86DiskProfileData,
  cpm86DiskProfileDataLarge,
  cpm86DiskProfileDataBig,
  cpm86DiskProfileSystemBig
} cpm86DiskProfile;

typedef struct
{
  char path[cpm86DrivePathCapacity];
  cpm86DiskProfile profile;
  bool readOnly;
  bool configured;
  bool profileFromSize;
} cpm86DriveConfig;

typedef enum
{
  cpm86DriveConfigLoaded,
  cpm86DriveConfigMissing,
  cpm86DriveConfigInvalid
} cpm86DriveConfigResult;

cpm86DriveConfigResult cpm86DriveConfigLoad(const char* path,
                                            cpm86DriveConfig drives[cpm86DiskDriveCount],
                                            char* error, size_t errorCapacity);

//-- Maps the size of the A: image to its profile (163840 = CPM86_160K, 528384 = CPM86_516K,
// 8392704 = CPM86_8196K); false for any other size.
bool cpm86DriveConfigSystemProfileFromSize(uint64_t imageSize, cpm86DiskProfile* profile);

//-- Sets drive->profile for A: from imageSize. An explicit size identifier in drives.cfg
//-- (profileFromSize false) must match the file size.
bool cpm86DriveConfigResolveSystemProfile(cpm86DriveConfig* drive, uint64_t imageSize, char* error,
                                          size_t errorCapacity);

//-- Pure BIOS record-geometry gate shared by cpm86Machine and the host boot fixture: track 0 is
//-- the reserved boot track, sectors run 0..31, tracks is the profile's track count (must be >1).
//-- Bounds against the real image size remain a separate check in the caller.
bool cpm86DriveRecordValid(uint32_t tracks, uint32_t track, uint32_t sector);
