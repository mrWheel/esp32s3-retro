#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cpmCpu.h"

enum
{
  cpmCcpAddress = 0xC400,
  cpmCcpSize = 0x0800,
  cpmBdosAddress = 0xCC00,
  cpmBdosSize = 0x0E00,
  cpmBiosAddress = 0xDA00,
  cpmBiosServicePort = 0xFE,
  cpmSystemImageSize = 256256,
  cpmSystemHeaderOffset = cpmCcpSize + cpmBdosSize,
  cpmSystemHeaderSize = 16,
  cpmDiskTracks = 77,
  cpmDiskSectorsPerTrack = 26,
  cpmDiskSectorSize = 128,
  cpmDiskBlockSize = 1024,
  cpmDiskDirectoryEntries = 64,
  cpmDiskDriveCount = 5,
  cpmDmaAddress = 0x0080
};

typedef struct
{
  bool (*consoleAvailable)(void *context);
  int (*consoleRead)(void *context);
  void (*consoleWrite)(void *context, uint8_t character);
  bool (*diskDriveAvailable)(void *context, uint8_t drive);
  bool (*diskReadRecord)(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                         uint8_t record[cpmDiskSectorSize]);
  bool (*diskWriteRecord)(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                          const uint8_t record[cpmDiskSectorSize]);
  uint8_t (*exchangePortInput)(void *context, uint8_t port);
  void (*exchangePortOutput)(void *context, uint8_t port, uint8_t value);
  void (*yield)(void *context);
  void *context;
} cpmHostOps;

typedef struct
{
  cpmCpu cpu;
  cpmHostOps host;
  uint8_t ccpImage[cpmCcpSize];
  uint8_t bdosImage[cpmBdosSize];
  uint16_t currentTrack;
  uint16_t currentSector;
  uint16_t dmaAddress;
  uint8_t selectedDrive;
  uint8_t keyboardCharacter;
  bool keyboardCharacterAvailable;
  bool skipLineFeed;
  bool initialized;
} cpmGuest;

//— Initialize guest instances with {0}; destroy them before initializing them again.
bool cpmGuestInitialize(cpmGuest *guest, const cpmHostOps *host, const uint8_t *ccpImage, const uint8_t *bdosImage);
void cpmGuestDestroy(cpmGuest *guest);
bool cpmGuestColdBoot(cpmGuest *guest);
bool cpmGuestStep(cpmGuest *guest);
size_t cpmGuestRunFor(cpmGuest *guest, size_t instructionBudget);
