#pragma once

#include "apple2Core.h"

enum
{
  apple2DiskSectorsPerTrack = 16,
  apple2DiskSectorSize = 256,
  apple2DiskTrackCount = 35,
  apple2DiskNibblesPerSector = 416,
  apple2DiskNibblesPerTrack = apple2DiskSectorsPerTrack * apple2DiskNibblesPerSector,
  apple2DiskCyclesPerNibble = 32,
  apple2DiskMaxHalfTrack = 69,
  apple2DiskMotorCoastCycles = 1000000,
  apple2DiskVolumeNumber = 254
};

typedef struct
{
  apple2DiskReadSectorFunction readSector;
  void *context;
  bool attached;
  bool motorOn;
  uint64_t spinUntilCycle;
  bool drive2Selected;
  bool q6;
  bool q7;
  uint8_t phases;
  uint8_t halfTrack;
  uint8_t latch;
  int32_t lastDeliveredNibble;
  int16_t cachedTrack;
  int16_t cachedSector;
  uint32_t sectorReads;
  uint32_t sectorReadFailures;
  uint32_t writeAttempts;
  uint8_t sectorNibbles[apple2DiskNibblesPerSector];
} apple2Disk;

void apple2DiskInitialize(apple2Disk *disk);
void apple2DiskResetSwitches(apple2Disk *disk);
bool apple2DiskAttach(apple2Disk *disk, apple2DiskReadSectorFunction readSector, void *context);
void apple2DiskDetach(apple2Disk *disk);
uint8_t apple2DiskAccess(apple2Disk *disk, uint8_t offset, bool write, uint64_t cycles, uint8_t busValue);
void apple2DiskGetState(const apple2Disk *disk, apple2DiskState *state);
