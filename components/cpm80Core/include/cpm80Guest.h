#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cpm80Cpu.h"

enum
{
  cpm80CcpAddress = 0xC400,
  cpm80CcpSize = 0x0800,
  cpm80BdosAddress = 0xCC00,
  cpm80BdosSize = 0x0E00,
  cpm80BiosAddress = 0xDA00,
  cpm80BiosServicePort = 0xFE,
  cpm80SystemImageSize = 256256,
  cpm80LargeImageSize = 512512,
  cpm80BigImageSize = 8421376,
  cpm80SystemHeaderOffset = cpm80CcpSize + cpm80BdosSize,
  cpm80SystemHeaderSize = 16,
  cpm80DiskTracks = 77,
  cpm80DiskSectorsPerTrack = 26,
  cpm80LargeDiskSectorsPerTrack = 52,
  cpm80BigDiskTracks = 514,
  cpm80BigDiskSectorsPerTrack = 128,
  cpm80BigDiskBlockSize = 16384,
  cpm80BigDiskDirectoryEntries = 512,
  cpm80DiskSectorSize = 128,
  cpm80DiskBlockSize = 1024,
  cpm80DiskDirectoryEntries = 64,
  cpm80LargeDiskBlockSize = 2048,
  cpm80LargeDiskDirectoryEntries = 128,
  cpm80DiskDriveCount = 6,
  cpm80DmaAddress = 0x0080
};

typedef enum
{
  cpm80DiskProfileSystem,
  cpm80DiskProfileLarge,
  cpm80DiskProfileBig
} cpm80DiskProfile;

typedef struct
{
  bool (*consoleAvailable)(void *context);
  int (*consoleRead)(void *context);
  void (*consoleWrite)(void *context, uint8_t character);
  bool (*diskDriveAvailable)(void *context, uint8_t drive);
  bool (*diskDriveProfile)(void *context, uint8_t drive, cpm80DiskProfile *profile);
  bool (*diskReadRecord)(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                         uint8_t record[cpm80DiskSectorSize]);
  bool (*diskWriteRecord)(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                          const uint8_t record[cpm80DiskSectorSize]);
  uint8_t (*exchangePortInput)(void *context, uint8_t port);
  void (*exchangePortOutput)(void *context, uint8_t port, uint8_t value);
  void (*yield)(void *context);
  void *context;
} cpm80HostOps;

typedef struct
{
  cpm80Cpu cpu;
  cpm80HostOps host;
  uint8_t ccpImage[cpm80CcpSize];
  uint8_t bdosImage[cpm80BdosSize];
  uint16_t currentTrack;
  uint16_t currentSector;
  uint16_t dmaAddress;
  cpm80DiskProfile diskProfiles[cpm80DiskDriveCount];
  uint8_t selectedDrive;
  uint8_t keyboardCharacter;
  bool keyboardCharacterAvailable;
  bool skipLineFeed;
  bool initialized;
} cpm80Guest;

uint16_t cpm80DiskProfileSectorsPerTrack(cpm80DiskProfile profile);
uint16_t cpm80DiskProfileTracks(cpm80DiskProfile profile);
uint64_t cpm80DiskProfileImageSize(cpm80DiskProfile profile);
const char *cpm80DiskProfileName(cpm80DiskProfile profile);

//— Initialize guest instances with {0}; destroy them before initializing them again.
bool cpm80GuestInitialize(cpm80Guest *guest, const cpm80HostOps *host, const uint8_t *ccpImage, const uint8_t *bdosImage);
void cpm80GuestDestroy(cpm80Guest *guest);
bool cpm80GuestColdBoot(cpm80Guest *guest);
bool cpm80GuestStep(cpm80Guest *guest);
size_t cpm80GuestRunFor(cpm80Guest *guest, size_t instructionBudget);
