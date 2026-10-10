#include "apple2Disk.h"
#include <string.h>

//-- Six-bit value to disk byte for the 16-sector (6-and-2) format.
static const uint8_t gcrWriteTable[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

//-- Physical (on-track) sector to image sector, per image order.
//-- Dos: DOS 3.3 logical sector (the order of sectors inside a .do/.dsk image).
//-- Prodos: ProDOS/Apple Pascal logical sector (the order inside a .po image; sectors 2n and 2n+1 form block n).
static const uint8_t imageSectorOfPhysical[2][apple2DiskSectorsPerTrack] = {
    {0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15},
    {0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15}};

static void resetDriveCache(apple2DiskDrive *drive)
{
  drive->cachedTrack = -1;
  drive->cachedSector = -1;
}

void apple2DiskInitialize(apple2Disk *disk)
{
  memset(disk, 0, sizeof(*disk));
  disk->lastDeliveredNibble = -1;
  for (size_t index = 0; index < apple2DiskDrivesPerSlot; ++index)
  {
    disk->drives[index].trackCount = apple2DiskTrackCount;
    resetDriveCache(&disk->drives[index]);
  }
}

void apple2DiskResetSwitches(apple2Disk *disk)
{
  disk->motorOn = false;
  disk->spinUntilCycle = 0;
  disk->drive2Selected = false;
  disk->q6 = false;
  disk->q7 = false;
  disk->writeLatchValid = false;
  disk->phases = 0;
  disk->latch = 0;
  disk->lastDeliveredNibble = -1;
  for (size_t index = 0; index < apple2DiskDrivesPerSlot; ++index)
  {
    disk->drives[index].writeDataCount = 0;
    disk->drives[index].writePrologueState = 0;
    disk->drives[index].writeDataActive = false;
  }
}

static bool attachDrive(apple2Disk *disk, uint8_t drive, apple2DiskSectorOrder order, uint8_t trackCount,
                        apple2DiskReadSectorFunction readSector, apple2DiskWriteSectorFunction writeSector,
                        void *context)
{
  if (disk == NULL || readSector == NULL || drive >= apple2DiskDrivesPerSlot || trackCount == 0 ||
      trackCount > apple2DiskMaxTracks ||
      (order != apple2DiskSectorOrderDos && order != apple2DiskSectorOrderProdos))
  {
    return false;
  }
  apple2DiskDrive *target = &disk->drives[drive];
  target->readSector = readSector;
  target->writeSector = writeSector;
  target->context = context;
  target->attached = true;
  target->order = order;
  target->trackCount = trackCount;
  target->writeDataCount = 0;
  target->writePrologueState = 0;
  target->writeDataActive = false;
  disk->writeLatchValid = false;
  if (target->halfTrack > (uint16_t)(trackCount * 2 - 1))
  {
    target->halfTrack = (uint16_t)(trackCount * 2 - 1);
  }
  resetDriveCache(target);
  disk->lastDeliveredNibble = -1;
  return true;
}

bool apple2DiskAttachDrive(apple2Disk *disk, uint8_t drive, apple2DiskSectorOrder order, uint8_t trackCount,
                           apple2DiskReadSectorFunction readSector, void *context)
{
  return attachDrive(disk, drive, order, trackCount, readSector, NULL, context);
}

bool apple2DiskAttachWritableDrive(apple2Disk *disk, uint8_t drive, apple2DiskSectorOrder order, uint8_t trackCount,
                                   apple2DiskReadSectorFunction readSector,
                                   apple2DiskWriteSectorFunction writeSector, void *context)
{
  return writeSector != NULL && attachDrive(disk, drive, order, trackCount, readSector, writeSector, context);
}

bool apple2DiskAttach(apple2Disk *disk, apple2DiskReadSectorFunction readSector, void *context)
{
  return apple2DiskAttachDrive(disk, 0, apple2DiskSectorOrderDos, apple2DiskTrackCount, readSector, context);
}

void apple2DiskDetachDrive(apple2Disk *disk, uint8_t drive)
{
  if (disk == NULL || drive >= apple2DiskDrivesPerSlot)
  {
    return;
  }
  apple2DiskDrive *target = &disk->drives[drive];
  target->readSector = NULL;
  target->writeSector = NULL;
  target->context = NULL;
  target->attached = false;
  target->order = apple2DiskSectorOrderDos;
  target->trackCount = apple2DiskTrackCount;
  resetDriveCache(target);
  target->writeDataCount = 0;
  target->writePrologueState = 0;
  target->writeDataActive = false;
  disk->writeLatchValid = false;
  disk->lastDeliveredNibble = -1;
}

bool apple2DiskIsAttached(const apple2Disk *disk)
{
  for (size_t index = 0; index < apple2DiskDrivesPerSlot; ++index)
  {
    if (disk->drives[index].attached)
    {
      return true;
    }
  }
  return false;
}

void apple2DiskDetach(apple2Disk *disk)
{
  for (uint8_t index = 0; index < apple2DiskDrivesPerSlot; ++index)
  {
    apple2DiskDetachDrive(disk, index);
  }
}

static void encodeFourAndFour(uint8_t *out, uint8_t value)
{
  out[0] = (uint8_t)((value >> 1) | 0xAA);
  out[1] = (uint8_t)(value | 0xAA);
}

static void encodeSector(uint8_t *nibbles, uint8_t track, uint8_t physicalSector, const uint8_t *data)
{
  memset(nibbles, 0xFF, apple2DiskNibblesPerSector);
  nibbles[40] = 0xD5;
  nibbles[41] = 0xAA;
  nibbles[42] = 0x96;
  encodeFourAndFour(&nibbles[43], apple2DiskVolumeNumber);
  encodeFourAndFour(&nibbles[45], track);
  encodeFourAndFour(&nibbles[47], physicalSector);
  encodeFourAndFour(&nibbles[49], (uint8_t)(apple2DiskVolumeNumber ^ track ^ physicalSector));
  nibbles[51] = 0xDE;
  nibbles[52] = 0xAA;
  nibbles[53] = 0xEB;
  nibbles[60] = 0xD5;
  nibbles[61] = 0xAA;
  nibbles[62] = 0xAD;

  uint8_t values[342];
  memset(values, 0, 86);
  for (size_t index = 0; index < apple2DiskSectorSize; ++index)
  {
    uint8_t low = (uint8_t)(data[index] & 3U);
    uint8_t reversed = (uint8_t)(((low & 1U) << 1) | (low >> 1));
    values[index % 86] |= (uint8_t)(reversed << (2 * (index / 86)));
    values[86 + index] = (uint8_t)(data[index] >> 2);
  }
  uint8_t previous = 0;
  for (size_t index = 0; index < sizeof(values); ++index)
  {
    nibbles[63 + index] = gcrWriteTable[values[index] ^ previous];
    previous = values[index];
  }
  nibbles[63 + sizeof(values)] = gcrWriteTable[previous];
  nibbles[406] = 0xDE;
  nibbles[407] = 0xAA;
  nibbles[408] = 0xEB;
}

static bool decodeWriteData(const uint8_t *nibbles, uint8_t *data)
{
  uint8_t values[342];
  uint8_t previous = 0;
  for (size_t index = 0; index < sizeof(values); ++index)
  {
    int encodedValue = -1;
    for (size_t value = 0; value < sizeof(gcrWriteTable); ++value)
    {
      if (gcrWriteTable[value] == nibbles[index])
      {
        encodedValue = (int)value;
        break;
      }
    }
    if (encodedValue < 0)
    {
      return false;
    }
    values[index] = (uint8_t)(encodedValue ^ previous);
    previous = values[index];
  }

  int checksumValue = -1;
  for (size_t value = 0; value < sizeof(gcrWriteTable); ++value)
  {
    if (gcrWriteTable[value] == nibbles[sizeof(values)])
    {
      checksumValue = (int)value;
      break;
    }
  }
  if (checksumValue < 0 || (uint8_t)checksumValue != previous)
  {
    return false;
  }

  for (size_t index = 0; index < apple2DiskSectorSize; ++index)
  {
    uint8_t reversedLowBits = (uint8_t)((values[index % 86] >> (2 * (index / 86))) & 3U);
    uint8_t lowBits = (uint8_t)(((reversedLowBits & 1U) << 1) | (reversedLowBits >> 1));
    data[index] = (uint8_t)((values[86 + index] << 2) | lowBits);
  }
  return true;
}

static void finishWriteData(apple2Disk *disk, apple2DiskDrive *drive)
{
  uint8_t data[apple2DiskSectorSize];
  if (drive->writeSector == NULL || !decodeWriteData(drive->writeDataNibbles, data) ||
      !drive->writeSector(drive->context, drive->writeTrack,
                          imageSectorOfPhysical[drive->order][drive->writePhysicalSector], data))
  {
    disk->writeFailures++;
  }
  else
  {
    resetDriveCache(drive);
  }
  drive->writeDataActive = false;
  drive->writeDataCount = 0;
  drive->writePrologueState = 0;
}

static void writeDataNibble(apple2Disk *disk, apple2DiskDrive *drive, uint8_t value, uint64_t cycles)
{
  if (drive->writeDataActive)
  {
    drive->writeDataNibbles[drive->writeDataCount++] = value;
    if (drive->writeDataCount == sizeof(drive->writeDataNibbles))
    {
      finishWriteData(disk, drive);
    }
    return;
  }

  if ((drive->writePrologueState == 0 || drive->writePrologueState == 1) && value == 0xD5)
  {
    drive->writePrologueState = 1;
    return;
  }
  if (drive->writePrologueState == 1 && value == 0xAA)
  {
    drive->writePrologueState = 2;
    return;
  }
  if (drive->writePrologueState == 2 && value == 0xAD)
  {
    uint32_t trackPosition = (uint32_t)((cycles / apple2DiskCyclesPerNibble) % apple2DiskNibblesPerTrack);
    drive->writeTrack = (uint8_t)(drive->halfTrack >> 1);
    drive->writePhysicalSector = (uint8_t)(trackPosition / apple2DiskNibblesPerSector);
    drive->writeDataCount = 0;
    drive->writeDataActive = true;
    drive->writePrologueState = 0;
    disk->writeAttempts++;
    return;
  }
  drive->writePrologueState = 0;
}

static apple2DiskDrive *selectedDrive(apple2Disk *disk)
{
  return &disk->drives[disk->drive2Selected ? 1 : 0];
}

static void loadSector(apple2Disk *disk, apple2DiskDrive *drive, uint8_t track, uint8_t physicalSector)
{
  uint8_t data[apple2DiskSectorSize];
  drive->cachedTrack = track;
  drive->cachedSector = physicalSector;
  if (drive->readSector != NULL &&
      drive->readSector(drive->context, track, imageSectorOfPhysical[drive->order][physicalSector], data))
  {
    disk->sectorReads++;
    encodeSector(drive->sectorNibbles, track, physicalSector, data);
    return;
  }
  disk->sectorReadFailures++;
  memset(drive->sectorNibbles, 0xFF, sizeof(drive->sectorNibbles));
}

static uint8_t nibbleAt(apple2Disk *disk, apple2DiskDrive *drive, uint32_t index)
{
  if ((drive->halfTrack & 1U) != 0 || (drive->halfTrack >> 1) >= drive->trackCount)
  {
    return 0xFF;
  }
  uint8_t track = (uint8_t)(drive->halfTrack >> 1);
  uint8_t sector = (uint8_t)(index / apple2DiskNibblesPerSector);
  if (drive->cachedTrack != track || drive->cachedSector != sector)
  {
    loadSector(disk, drive, track, sector);
  }
  return drive->sectorNibbles[index % apple2DiskNibblesPerSector];
}

//-- Simplified stepper: each phase-on event moves the selected drive one half-track toward the net pull of the
//-- energized magnets. The head stops at the last half-track of the drive's medium (35 tracks without a medium).
static void stepHead(apple2Disk *disk)
{
  apple2DiskDrive *drive = selectedDrive(disk);
  int pull = 0;
  for (int phase = 0; phase < 4; ++phase)
  {
    if ((disk->phases & (1U << phase)) != 0)
    {
      int distance = (phase - (int)drive->halfTrack) & 3;
      pull += distance == 1 ? 1 : (distance == 3 ? -1 : 0);
    }
  }
  if (pull > 0 && drive->halfTrack < (uint16_t)(drive->trackCount * 2 - 1))
  {
    drive->halfTrack++;
    disk->lastDeliveredNibble = -1;
  }
  else if (pull < 0 && drive->halfTrack > 0)
  {
    drive->halfTrack--;
    disk->lastDeliveredNibble = -1;
  }
}

//-- The medium keeps turning for a coast-down period after the motor switch is turned off.
static uint8_t readDataLatch(apple2Disk *disk, uint64_t cycles)
{
  apple2DiskDrive *drive = selectedDrive(disk);
  bool spinning = disk->motorOn || cycles < disk->spinUntilCycle;
  if (!spinning || !drive->attached)
  {
    return 0x00;
  }
  uint32_t index = (uint32_t)((cycles / apple2DiskCyclesPerNibble) % apple2DiskNibblesPerTrack);
  if ((int32_t)index != disk->lastDeliveredNibble)
  {
    disk->lastDeliveredNibble = (int32_t)index;
    disk->latch = nibbleAt(disk, drive, index);
    return disk->latch;
  }
  return (uint8_t)(disk->latch & 0x7F);
}

uint8_t apple2DiskAccess(apple2Disk *disk, uint8_t offset, bool write, uint64_t cycles, uint8_t busValue)
{
  bool on = (offset & 1U) != 0;
  switch (offset >> 1)
  {
  case 0:
  case 1:
  case 2:
  case 3:
  {
    uint8_t bit = (uint8_t)(1U << (offset >> 1));
    if (on)
    {
      if ((disk->phases & bit) == 0)
      {
        disk->phases |= bit;
        stepHead(disk);
      }
    }
    else
    {
      disk->phases &= (uint8_t)~bit;
    }
    break;
  }
  case 4:
    if (on)
    {
      disk->spinUntilCycle = 0;
    }
    else if (disk->motorOn)
    {
      disk->spinUntilCycle = cycles + apple2DiskMotorCoastCycles;
    }
    disk->motorOn = on;
    break;
  case 5:
    if (disk->drive2Selected != on)
    {
      apple2DiskDrive *previousDrive = selectedDrive(disk);
      previousDrive->writeDataCount = 0;
      previousDrive->writePrologueState = 0;
      previousDrive->writeDataActive = false;
      disk->writeLatchValid = false;
      disk->drive2Selected = on;
      apple2DiskDrive *nextDrive = selectedDrive(disk);
      nextDrive->writeDataCount = 0;
      nextDrive->writePrologueState = 0;
      nextDrive->writeDataActive = false;
      disk->lastDeliveredNibble = -1;
    }
    break;
  case 6:
    disk->q6 = on;
    break;
  default:
    disk->q7 = on;
    if (!on)
    {
      apple2DiskDrive *drive = selectedDrive(disk);
      drive->writeDataCount = 0;
      drive->writePrologueState = 0;
      drive->writeDataActive = false;
      disk->writeLatchValid = false;
    }
    break;
  }
  apple2DiskDrive *drive = selectedDrive(disk);
  bool spinning = disk->motorOn || cycles < disk->spinUntilCycle;
  if (disk->q7 && drive->attached && spinning)
  {
    if (disk->q6 && write)
    {
      disk->latch = busValue;
      disk->writeLatchValid = true;
    }
    else if (!disk->q6 && offset == 0xC && disk->writeLatchValid &&
             (drive->halfTrack & 1U) == 0 && (drive->halfTrack >> 1) < drive->trackCount)
    {
      writeDataNibble(disk, drive, disk->latch, cycles);
      disk->writeLatchValid = false;
    }
  }
  if (write)
  {
    return busValue;
  }
  if (offset < 0xC || on)
  {
    return busValue;
  }
  if (disk->q7)
  {
    return 0x00;
  }
  if (disk->q6)
  {
    apple2DiskDrive *drive = selectedDrive(disk);
    return drive->attached && drive->writeSector == NULL ? 0x80 : 0x00;
  }
  return readDataLatch(disk, cycles);
}

void apple2DiskGetState(const apple2Disk *disk, apple2DiskState *state)
{
  state->attached = apple2DiskIsAttached(disk);
  state->motorOn = disk->motorOn;
  state->drive2Selected = disk->drive2Selected;
  state->q6 = disk->q6;
  state->q7 = disk->q7;
  state->phases = disk->phases;
  state->halfTrack = disk->drives[disk->drive2Selected ? 1 : 0].halfTrack;
  state->sectorReads = disk->sectorReads;
  state->sectorReadFailures = disk->sectorReadFailures;
  state->writeAttempts = disk->writeAttempts;
  state->writeFailures = disk->writeFailures;
}
