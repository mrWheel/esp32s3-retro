#include "cpm80Guest.h"
#include "hostExchange.h"
#include <string.h>

enum
{
  cpm80BiosFunctionCount = 17,
  cpm80BiosVectorSize = 3,
  cpm80BiosStubSize = 5,
  cpm80BiosStubAddress = cpm80BiosAddress + cpm80BiosFunctionCount * cpm80BiosVectorSize,
  cpm80BiosDphAddress = 0xDA90,
  cpm80BiosDpbAddress = 0xDAA0,
  cpm80BiosDirectoryBufferAddress = 0xDAB0,
  cpm80BiosCsvAddress = 0xDB30,
  cpm80BiosAlvAddress = 0xDB40,
  cpm80BiosAdditionalDphAddress = 0xDB60,
  cpm80BiosAdditionalDriveStride = 0x0100,
  cpm80BiosAdditionalDpbOffset = 0x10,
  cpm80BiosAdditionalDirectoryBufferOffset = 0x20,
  cpm80BiosAdditionalCsvOffset = 0xA0,
  cpm80BiosAdditionalAlvOffset = 0xC0,
  cpm80BiosCsvSize = 16,
  cpm80BiosLargeCsvSize = 32,
  cpm80BiosAlvSize = 31,
  cpm80BiosBigAlvSize = 64,
  cpm80BdosEntryAddress = cpm80BdosAddress + 6,
  cpm80VirtualDpbSpt = 26,
  cpm80VirtualDpbBsh = 3,
  cpm80VirtualDpbBlm = 7,
  cpm80VirtualDpbExm = 0,
  cpm80VirtualDpbDsm = 242,
  cpm80VirtualDpbDrm = 63,
  cpm80VirtualDpbAl0 = 0xC0,
  cpm80VirtualDpbAl1 = 0,
  cpm80VirtualDpbCks = 16,
  cpm80VirtualDpbOff = 2,
  cpm80LargeVirtualDpbSpt = 52,
  cpm80LargeVirtualDpbBsh = 4,
  cpm80LargeVirtualDpbBlm = 15,
  cpm80LargeVirtualDpbExm = 0,
  cpm80LargeVirtualDpbDsm = 242,
  cpm80LargeVirtualDpbDrm = 127,
  cpm80LargeVirtualDpbAl0 = 0xC0,
  cpm80LargeVirtualDpbAl1 = 0,
  cpm80LargeVirtualDpbCks = 32,
  cpm80LargeVirtualDpbOff = 2,
  cpm80BigVirtualDpbBsh = 7,
  cpm80BigVirtualDpbBlm = 127,
  cpm80BigVirtualDpbExm = 7,
  cpm80BigVirtualDpbDsm = 511,
  cpm80BigVirtualDpbDrm = 511,
  cpm80BigVirtualDpbAl0 = 0x80,
  cpm80BigVirtualDpbAl1 = 0,
  cpm80BigVirtualDpbCks = 0,
  cpm80BigVirtualDpbOff = 2
};

typedef enum
{
  cpm80BiosBoot,
  cpm80BiosWarmBoot,
  cpm80BiosConst,
  cpm80BiosConin,
  cpm80BiosConout,
  cpm80BiosList,
  cpm80BiosPunch,
  cpm80BiosReader,
  cpm80BiosHome,
  cpm80BiosSeldsk,
  cpm80BiosSettrk,
  cpm80BiosSetsec,
  cpm80BiosSetdma,
  cpm80BiosRead,
  cpm80BiosWrite,
  cpm80BiosListst,
  cpm80BiosSectran
} cpm80BiosFunction;

static uint16_t getRegisterPair(uint8_t high, uint8_t low)
{
  return (uint16_t)(((uint16_t)high << 8) | low);
}

static void setRegisterPair(uint8_t *high, uint8_t *low, uint16_t value)
{
  *high = (uint8_t)(value >> 8);
  *low = (uint8_t)value;
}

static void writeWord(uint8_t *memory, uint16_t address, uint16_t value)
{
  memory[address] = (uint8_t)value;
  memory[address + 1] = (uint8_t)(value >> 8);
}

static void resetProcessor(cpm80Guest *guest, uint16_t programCounter)
{
  z80 *processor = &guest->cpu.processor;
  processor->cyc = 0;
  processor->pc = programCounter;
  processor->sp = 0x0100;
  processor->ix = 0;
  processor->iy = 0;
  processor->mem_ptr = 0;
  processor->a = 0;
  processor->b = 0;
  processor->c = 0;
  processor->d = 0;
  processor->e = 0;
  processor->h = 0;
  processor->l = 0;
  processor->a_ = 0;
  processor->b_ = 0;
  processor->c_ = 0;
  processor->d_ = 0;
  processor->e_ = 0;
  processor->h_ = 0;
  processor->l_ = 0;
  processor->f_ = 0;
  processor->i = 0;
  processor->r = 0;
  processor->sf = false;
  processor->zf = false;
  processor->yf = false;
  processor->hf = false;
  processor->xf = false;
  processor->pf = false;
  processor->nf = false;
  processor->cf = false;
  processor->iff_delay = 0;
  processor->interrupt_mode = 0;
  processor->int_data = 0;
  processor->iff1 = false;
  processor->iff2 = false;
  processor->halted = false;
  processor->int_pending = false;
  processor->nmi_pending = false;
}

static void installBios(cpm80Guest *guest)
{
  uint8_t *memory = guest->cpu.memory;

  for (uint8_t drive = 0; drive < cpm80DiskDriveCount; ++drive)
  {
    uint16_t dphAddress =
        drive == 0 ? cpm80BiosDphAddress
                   : cpm80BiosAdditionalDphAddress + (uint16_t)(drive - 1) * cpm80BiosAdditionalDriveStride;
    uint16_t dpbAddress = drive == 0 ? cpm80BiosDpbAddress : dphAddress + cpm80BiosAdditionalDpbOffset;
    uint16_t directoryBufferAddress =
        drive == 0 ? cpm80BiosDirectoryBufferAddress : dphAddress + cpm80BiosAdditionalDirectoryBufferOffset;
    uint16_t csvAddress = drive == 0 ? cpm80BiosCsvAddress : dphAddress + cpm80BiosAdditionalCsvOffset;
    uint16_t alvAddress = drive == 0 ? cpm80BiosAlvAddress : dphAddress + cpm80BiosAdditionalAlvOffset;
    cpm80DiskProfile profile = guest->diskProfiles[drive];
    bool large = profile == cpm80DiskProfileLarge;
    bool big = profile == cpm80DiskProfileBig;
    uint16_t spt = cpm80DiskProfileSectorsPerTrack(profile);
    uint16_t dsm = big ? cpm80BigVirtualDpbDsm : large ? cpm80LargeVirtualDpbDsm : cpm80VirtualDpbDsm;
    uint16_t drm = big ? cpm80BigVirtualDpbDrm : large ? cpm80LargeVirtualDpbDrm : cpm80VirtualDpbDrm;
    uint16_t cks = big ? cpm80BigVirtualDpbCks : large ? cpm80LargeVirtualDpbCks : cpm80VirtualDpbCks;
    uint16_t off = big ? cpm80BigVirtualDpbOff : large ? cpm80LargeVirtualDpbOff : cpm80VirtualDpbOff;
    uint8_t bsh = big ? cpm80BigVirtualDpbBsh : large ? cpm80LargeVirtualDpbBsh : cpm80VirtualDpbBsh;
    uint8_t blm = big ? cpm80BigVirtualDpbBlm : large ? cpm80LargeVirtualDpbBlm : cpm80VirtualDpbBlm;
    uint8_t exm = big ? cpm80BigVirtualDpbExm : large ? cpm80LargeVirtualDpbExm : cpm80VirtualDpbExm;
    uint8_t al0 = big ? cpm80BigVirtualDpbAl0 : large ? cpm80LargeVirtualDpbAl0 : cpm80VirtualDpbAl0;
    uint8_t al1 = big ? cpm80BigVirtualDpbAl1 : large ? cpm80LargeVirtualDpbAl1 : cpm80VirtualDpbAl1;
    uint16_t csvSize = big ? 0 : large ? cpm80BiosLargeCsvSize : cpm80BiosCsvSize;
    uint16_t alvSize = big ? cpm80BiosBigAlvSize : cpm80BiosAlvSize;

    memset(memory + dphAddress, 0, 16);
    writeWord(memory, dphAddress + 8, directoryBufferAddress);
    writeWord(memory, dphAddress + 10, dpbAddress);
    writeWord(memory, dphAddress + 12, csvAddress);
    writeWord(memory, dphAddress + 14, alvAddress);
    writeWord(memory, dpbAddress, spt);
    memory[dpbAddress + 2] = bsh;
    memory[dpbAddress + 3] = blm;
    memory[dpbAddress + 4] = exm;
    writeWord(memory, dpbAddress + 5, dsm);
    writeWord(memory, dpbAddress + 7, drm);
    memory[dpbAddress + 9] = al0;
    memory[dpbAddress + 10] = al1;
    writeWord(memory, dpbAddress + 11, cks);
    writeWord(memory, dpbAddress + 13, off);
    memset(memory + directoryBufferAddress, 0, cpm80DiskSectorSize);
    memset(memory + csvAddress, 0, csvSize);
    memset(memory + alvAddress, 0, alvSize);
  }

  for (uint8_t function = 0; function < cpm80BiosFunctionCount; ++function)
  {
    uint16_t vectorAddress = cpm80BiosAddress + function * cpm80BiosVectorSize;
    uint16_t stubAddress = cpm80BiosStubAddress + function * cpm80BiosStubSize;
    memory[vectorAddress] = 0xC3;
    writeWord(memory, vectorAddress + 1, stubAddress);
    memory[stubAddress] = 0x3E;
    memory[stubAddress + 1] = function;
    memory[stubAddress + 2] = 0xD3;
    memory[stubAddress + 3] = cpm80BiosServicePort;
    memory[stubAddress + 4] = 0xC9;
  }

}

static void installSystem(cpm80Guest *guest, bool coldBoot)
{
  uint8_t *memory = guest->cpu.memory;
  uint8_t currentDisk = coldBoot ? 0 : memory[0x0004];
  if (coldBoot)
  {
    memset(memory, 0, cpm80MemorySize);
  }
  else
  {
    memset(memory, 0, 0x0100);
  }

  memcpy(memory + cpm80CcpAddress, guest->ccpImage, sizeof(guest->ccpImage));
  memcpy(memory + cpm80BdosAddress, guest->bdosImage, sizeof(guest->bdosImage));
  installBios(guest);

  memory[0x0000] = 0xC3;
  writeWord(memory, 0x0001, cpm80BiosAddress + cpm80BiosVectorSize);
  memory[0x0003] = 0;
  memory[0x0004] = currentDisk;
  memory[0x0005] = 0xC3;
  writeWord(memory, 0x0006, cpm80BdosEntryAddress);

  guest->selectedDrive = 0;
  guest->currentTrack = 0;
  guest->currentSector = 0;
  guest->dmaAddress = cpm80DmaAddress;
  if (coldBoot)
  {
    guest->keyboardCharacterAvailable = false;
    guest->skipLineFeed = false;
  }
  resetProcessor(guest, cpm80CcpAddress);
  guest->cpu.processor.c = currentDisk;
}

static bool readConsoleCharacter(cpm80Guest *guest, uint8_t *character)
{
  while (guest->host.consoleAvailable(guest->host.context))
  {
    int input = guest->host.consoleRead(guest->host.context);
    if (input < 0)
    {
      return false;
    }
    if (guest->skipLineFeed && input == '\n')
    {
      guest->skipLineFeed = false;
      continue;
    }
    guest->skipLineFeed = false;
    if (input == '\r')
    {
      guest->skipLineFeed = true;
      *character = '\r';
      return true;
    }
    *character = (uint8_t)(input == '\n' ? '\r' : input) & 0x7F;
    return true;
  }
  return false;
}

uint16_t cpm80DiskProfileSectorsPerTrack(cpm80DiskProfile profile)
{
  switch (profile)
  {
  case cpm80DiskProfileBig:
    return cpm80BigDiskSectorsPerTrack;
  case cpm80DiskProfileLarge:
    return cpm80LargeDiskSectorsPerTrack;
  default:
    return cpm80DiskSectorsPerTrack;
  }
}

uint16_t cpm80DiskProfileTracks(cpm80DiskProfile profile)
{
  return profile == cpm80DiskProfileBig ? cpm80BigDiskTracks : cpm80DiskTracks;
}

uint64_t cpm80DiskProfileImageSize(cpm80DiskProfile profile)
{
  switch (profile)
  {
  case cpm80DiskProfileBig:
    return cpm80BigImageSize;
  case cpm80DiskProfileLarge:
    return cpm80LargeImageSize;
  default:
    return cpm80SystemImageSize;
  }
}

const char *cpm80DiskProfileName(cpm80DiskProfile profile)
{
  switch (profile)
  {
  case cpm80DiskProfileBig:
    return "BIG";
  case cpm80DiskProfileLarge:
    return "LARGE";
  default:
    return "SYSTEM";
  }
}

static uint16_t sectorsPerTrack(cpm80DiskProfile profile)
{
  return cpm80DiskProfileSectorsPerTrack(profile);
}

static bool diskPositionValid(const cpm80Guest *guest)
{
  return guest->selectedDrive < cpm80DiskDriveCount && guest->currentTrack < cpm80DiskProfileTracks(guest->diskProfiles[guest->selectedDrive]) &&
         guest->currentSector < sectorsPerTrack(guest->diskProfiles[guest->selectedDrive]);
}

static bool readDiskRecord(cpm80Guest *guest)
{
  z80 *processor = &guest->cpu.processor;
  uint8_t record[cpm80DiskSectorSize];
  if (!diskPositionValid(guest) || guest->dmaAddress > cpm80MemorySize - cpm80DiskSectorSize ||
      !guest->host.diskReadRecord(guest->host.context, guest->selectedDrive, guest->currentTrack,
                                 guest->currentSector, record))
  {
    processor->a = 1;
    return false;
  }
  memcpy(guest->cpu.memory + guest->dmaAddress, record, sizeof(record));
  processor->a = 0;
  return true;
}

static bool writeDiskRecord(cpm80Guest *guest)
{
  z80 *processor = &guest->cpu.processor;
  if (!diskPositionValid(guest) || guest->dmaAddress > cpm80MemorySize - cpm80DiskSectorSize ||
      !guest->host.diskWriteRecord(guest->host.context, guest->selectedDrive, guest->currentTrack,
                                  guest->currentSector, guest->cpu.memory + guest->dmaAddress))
  {
    processor->a = 1;
    return false;
  }
  processor->a = 0;
  return true;
}

static void serviceBios(cpm80Guest *guest, uint8_t function)
{
  z80 *processor = &guest->cpu.processor;
  uint16_t registerBc = getRegisterPair(processor->b, processor->c);

  switch (function)
  {
  case cpm80BiosBoot:
    installSystem(guest, true);
    break;
  case cpm80BiosWarmBoot:
    installSystem(guest, false);
    break;
  case cpm80BiosConst:
    if (!guest->keyboardCharacterAvailable)
    {
      guest->keyboardCharacterAvailable = readConsoleCharacter(guest, &guest->keyboardCharacter);
    }
    processor->a = guest->keyboardCharacterAvailable ? 0xFF : 0;
    break;
  case cpm80BiosConin:
    if (!guest->keyboardCharacterAvailable)
    {
      guest->keyboardCharacterAvailable = readConsoleCharacter(guest, &guest->keyboardCharacter);
    }
    if (guest->keyboardCharacterAvailable)
    {
      processor->a = guest->keyboardCharacter;
      guest->keyboardCharacterAvailable = false;
    }
    else
    {
      processor->pc = cpm80BiosStubAddress + cpm80BiosConin * cpm80BiosStubSize;
      guest->host.yield(guest->host.context);
    }
    break;
  case cpm80BiosConout:
    guest->host.consoleWrite(guest->host.context, processor->c);
    break;
  case cpm80BiosList:
  case cpm80BiosPunch:
    break;
  case cpm80BiosReader:
    processor->a = 0x1A;
    break;
  case cpm80BiosHome:
    guest->currentTrack = 0;
    break;
  case cpm80BiosSeldsk:
    if (processor->c < cpm80DiskDriveCount &&
        guest->host.diskDriveAvailable(guest->host.context, processor->c))
    {
      guest->selectedDrive = processor->c;
      uint16_t dphAddress = processor->c == 0
                                ? cpm80BiosDphAddress
                                : cpm80BiosAdditionalDphAddress +
                                      (uint16_t)(processor->c - 1) * cpm80BiosAdditionalDriveStride;
      setRegisterPair(&processor->h, &processor->l, dphAddress);
    }
    else
    {
      setRegisterPair(&processor->h, &processor->l, 0);
    }
    break;
  case cpm80BiosSettrk:
    guest->currentTrack = registerBc;
    break;
  case cpm80BiosSetsec:
    guest->currentSector = registerBc;
    break;
  case cpm80BiosSetdma:
    guest->dmaAddress = registerBc;
    break;
  case cpm80BiosRead:
    readDiskRecord(guest);
    break;
  case cpm80BiosWrite:
    writeDiskRecord(guest);
    break;
  case cpm80BiosListst:
    processor->a = 0;
    break;
  case cpm80BiosSectran:
    setRegisterPair(&processor->h, &processor->l, registerBc);
    break;
  default:
    processor->pc = 0;
    processor->halted = true;
    break;
  }
}

static uint8_t readPort(void *context, uint8_t port)
{
  cpm80Guest *guest = context;
  if ((port == hostExchangePort || port == hostExchangeAbortPort) &&
      guest->host.exchangePortInput != NULL)
  {
    return guest->host.exchangePortInput(guest->host.context, port);
  }
  return 0xFF;
}

static void writePort(void *context, uint8_t port, uint8_t value)
{
  cpm80Guest *guest = context;
  if (port == cpm80BiosServicePort)
  {
    serviceBios(guest, value);
  }
  else if ((port == hostExchangePort || port == hostExchangeAbortPort) &&
           guest->host.exchangePortOutput != NULL)
  {
    guest->host.exchangePortOutput(guest->host.context, port, value);
  }
}

bool cpm80GuestInitialize(cpm80Guest *guest, const cpm80HostOps *host, const uint8_t *ccpImage, const uint8_t *bdosImage)
{
  if (guest == NULL || host == NULL || ccpImage == NULL || bdosImage == NULL ||
      host->consoleAvailable == NULL || host->consoleRead == NULL || host->consoleWrite == NULL ||
      host->diskDriveAvailable == NULL || host->diskDriveProfile == NULL || host->diskReadRecord == NULL ||
      host->diskWriteRecord == NULL ||
      host->yield == NULL ||
      guest->initialized || guest->cpu.memory != NULL)
  {
    return false;
  }

  guest->host = *host;
  for (uint8_t drive = 0; drive < cpm80DiskDriveCount; ++drive)
  {
    if (host->diskDriveAvailable(host->context, drive) &&
        (!host->diskDriveProfile(host->context, drive, &guest->diskProfiles[drive]) ||
         (guest->diskProfiles[drive] != cpm80DiskProfileSystem && guest->diskProfiles[drive] != cpm80DiskProfileLarge &&
          guest->diskProfiles[drive] != cpm80DiskProfileBig)))
    {
      memset(guest, 0, sizeof(*guest));
      return false;
    }
  }
  memcpy(guest->ccpImage, ccpImage, sizeof(guest->ccpImage));
  memcpy(guest->bdosImage, bdosImage, sizeof(guest->bdosImage));
  if (!cpm80CpuInitialize(&guest->cpu, readPort, writePort, guest))
  {
    memset(guest, 0, sizeof(*guest));
    return false;
  }
  guest->initialized = true;
  installSystem(guest, true);
  return true;
}

void cpm80GuestDestroy(cpm80Guest *guest)
{
  if (guest == NULL)
  {
    return;
  }
  cpm80CpuDestroy(&guest->cpu);
  memset(guest, 0, sizeof(*guest));
}

bool cpm80GuestColdBoot(cpm80Guest *guest)
{
  if (guest == NULL || !guest->initialized)
  {
    return false;
  }
  resetProcessor(guest, cpm80BiosAddress);
  return true;
}

bool cpm80GuestStep(cpm80Guest *guest)
{
  if (guest == NULL || !guest->initialized || guest->cpu.processor.halted)
  {
    return false;
  }
  return cpm80CpuStep(&guest->cpu);
}

size_t cpm80GuestRunFor(cpm80Guest *guest, size_t instructionBudget)
{
  if (guest == NULL || !guest->initialized)
  {
    return 0;
  }
  size_t instructions = 0;
  while (instructions < instructionBudget && cpm80GuestStep(guest))
  {
    ++instructions;
    if ((instructions & 0x0FFF) == 0)
    {
      guest->host.yield(guest->host.context);
    }
  }
  return instructions;
}
