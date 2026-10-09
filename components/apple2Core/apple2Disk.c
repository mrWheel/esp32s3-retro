#include "apple2Disk.h"
#include <string.h>

//-- Six-bit value to disk byte for the 16-sector (6-and-2) format.
static const uint8_t gcrWriteTable[64] = {
    0x96, 0x97, 0x9A, 0x9B, 0x9D, 0x9E, 0x9F, 0xA6, 0xA7, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xCB, 0xCD, 0xCE, 0xCF, 0xD3,
    0xD6, 0xD7, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF, 0xE5, 0xE6, 0xE7, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

//-- Physical (on-track) sector to DOS 3.3 logical sector, i.e. the order of sectors inside a .dsk image.
static const uint8_t logicalOfPhysical[apple2DiskSectorsPerTrack] = {0, 7, 14, 6, 13, 5, 12, 4,
                                                                      11, 3, 10, 2, 9, 1, 8, 15};

void apple2DiskInitialize(apple2Disk *disk)
{
  memset(disk, 0, sizeof(*disk));
  disk->lastDeliveredNibble = -1;
  disk->cachedTrack = -1;
  disk->cachedSector = -1;
}

void apple2DiskResetSwitches(apple2Disk *disk)
{
  disk->motorOn = false;
  disk->spinUntilCycle = 0;
  disk->drive2Selected = false;
  disk->q6 = false;
  disk->q7 = false;
  disk->phases = 0;
  disk->latch = 0;
  disk->lastDeliveredNibble = -1;
}

bool apple2DiskAttach(apple2Disk *disk, apple2DiskReadSectorFunction readSector, void *context)
{
  if (readSector == NULL)
  {
    return false;
  }
  disk->readSector = readSector;
  disk->context = context;
  disk->attached = true;
  disk->cachedTrack = -1;
  disk->cachedSector = -1;
  disk->lastDeliveredNibble = -1;
  return true;
}

void apple2DiskDetach(apple2Disk *disk)
{
  disk->readSector = NULL;
  disk->context = NULL;
  disk->attached = false;
  disk->cachedTrack = -1;
  disk->cachedSector = -1;
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

static void loadSector(apple2Disk *disk, uint8_t track, uint8_t physicalSector)
{
  uint8_t data[apple2DiskSectorSize];
  disk->cachedTrack = track;
  disk->cachedSector = physicalSector;
  if (disk->readSector != NULL &&
      disk->readSector(disk->context, track, logicalOfPhysical[physicalSector], data))
  {
    disk->sectorReads++;
    encodeSector(disk->sectorNibbles, track, physicalSector, data);
    return;
  }
  disk->sectorReadFailures++;
  memset(disk->sectorNibbles, 0xFF, sizeof(disk->sectorNibbles));
}

static uint8_t nibbleAt(apple2Disk *disk, uint32_t index)
{
  if ((disk->halfTrack & 1U) != 0 || (disk->halfTrack >> 1) >= apple2DiskTrackCount)
  {
    return 0xFF;
  }
  uint8_t track = (uint8_t)(disk->halfTrack >> 1);
  uint8_t sector = (uint8_t)(index / apple2DiskNibblesPerSector);
  if (disk->cachedTrack != track || disk->cachedSector != sector)
  {
    loadSector(disk, track, sector);
  }
  return disk->sectorNibbles[index % apple2DiskNibblesPerSector];
}

//-- Simplified stepper: each phase-on event moves one half-track toward the net pull of the energized magnets.
static void stepHead(apple2Disk *disk)
{
  int pull = 0;
  for (int phase = 0; phase < 4; ++phase)
  {
    if ((disk->phases & (1U << phase)) != 0)
    {
      int distance = (phase - (int)disk->halfTrack) & 3;
      pull += distance == 1 ? 1 : (distance == 3 ? -1 : 0);
    }
  }
  if (pull > 0 && disk->halfTrack < apple2DiskMaxHalfTrack)
  {
    disk->halfTrack++;
    disk->lastDeliveredNibble = -1;
  }
  else if (pull < 0 && disk->halfTrack > 0)
  {
    disk->halfTrack--;
    disk->lastDeliveredNibble = -1;
  }
}

//-- The medium keeps turning for a coast-down period after the motor switch is turned off.
static uint8_t readDataLatch(apple2Disk *disk, uint64_t cycles)
{
  bool spinning = disk->motorOn || cycles < disk->spinUntilCycle;
  if (!spinning || disk->drive2Selected)
  {
    return 0x00;
  }
  uint32_t index = (uint32_t)((cycles / apple2DiskCyclesPerNibble) % apple2DiskNibblesPerTrack);
  if ((int32_t)index != disk->lastDeliveredNibble)
  {
    disk->lastDeliveredNibble = (int32_t)index;
    disk->latch = nibbleAt(disk, index);
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
    disk->drive2Selected = on;
    break;
  case 6:
    disk->q6 = on;
    break;
  default:
    disk->q7 = on;
    break;
  }
  if (write)
  {
    if (disk->q6 && disk->q7)
    {
      disk->writeAttempts++;
    }
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
    return disk->drive2Selected ? 0x00 : 0x80;
  }
  return readDataLatch(disk, cycles);
}

void apple2DiskGetState(const apple2Disk *disk, apple2DiskState *state)
{
  state->attached = disk->attached;
  state->motorOn = disk->motorOn;
  state->drive2Selected = disk->drive2Selected;
  state->q6 = disk->q6;
  state->q7 = disk->q7;
  state->phases = disk->phases;
  state->halfTrack = disk->halfTrack;
  state->sectorReads = disk->sectorReads;
  state->sectorReadFailures = disk->sectorReadFailures;
  state->writeAttempts = disk->writeAttempts;
}
