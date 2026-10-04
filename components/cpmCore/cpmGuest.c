#include "cpmGuest.h"
#include <string.h>

enum
{
  cpmBiosFunctionCount = 17,
  cpmBiosVectorSize = 3,
  cpmBiosStubSize = 5,
  cpmBiosStubAddress = cpmBiosAddress + cpmBiosFunctionCount * cpmBiosVectorSize,
  cpmBiosDphAddress = 0xDA90,
  cpmBiosDpbAddress = 0xDAA0,
  cpmBiosDirectoryBufferAddress = 0xDAB0,
  cpmBiosCsvAddress = 0xDB30,
  cpmBiosAlvAddress = 0xDB40,
  cpmBiosAdditionalDphAddress = 0xDB60,
  cpmBiosAdditionalDriveStride = 0x00C0,
  cpmBiosAdditionalDirectoryBufferOffset = 0x10,
  cpmBiosAdditionalCsvOffset = 0x90,
  cpmBiosAdditionalAlvOffset = 0xA0,
  cpmBiosCsvSize = 16,
  cpmBiosAlvSize = 31,
  cpmBdosEntryAddress = cpmBdosAddress + 6,
  cpmVirtualDpbSpt = 26,
  cpmVirtualDpbBsh = 3,
  cpmVirtualDpbBlm = 7,
  cpmVirtualDpbExm = 0,
  cpmVirtualDpbDsm = 242,
  cpmVirtualDpbDrm = 63,
  cpmVirtualDpbAl0 = 0xC0,
  cpmVirtualDpbAl1 = 0,
  cpmVirtualDpbCks = 16,
  cpmVirtualDpbOff = 2
};

typedef enum
{
  cpmBiosBoot,
  cpmBiosWarmBoot,
  cpmBiosConst,
  cpmBiosConin,
  cpmBiosConout,
  cpmBiosList,
  cpmBiosPunch,
  cpmBiosReader,
  cpmBiosHome,
  cpmBiosSeldsk,
  cpmBiosSettrk,
  cpmBiosSetsec,
  cpmBiosSetdma,
  cpmBiosRead,
  cpmBiosWrite,
  cpmBiosListst,
  cpmBiosSectran
} cpmBiosFunction;

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

static void resetProcessor(cpmGuest *guest, uint16_t programCounter)
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

static void installBios(cpmGuest *guest)
{
  static const uint8_t dpb[] = {cpmVirtualDpbSpt,
                                cpmVirtualDpbSpt >> 8,
                                cpmVirtualDpbBsh,
                                cpmVirtualDpbBlm,
                                cpmVirtualDpbExm,
                                cpmVirtualDpbDsm,
                                cpmVirtualDpbDsm >> 8,
                                cpmVirtualDpbDrm,
                                cpmVirtualDpbDrm >> 8,
                                cpmVirtualDpbAl0,
                                cpmVirtualDpbAl1,
                                cpmVirtualDpbCks,
                                cpmVirtualDpbCks >> 8,
                                cpmVirtualDpbOff,
                                cpmVirtualDpbOff >> 8};
  uint8_t *memory = guest->cpu.memory;

  for (uint8_t function = 0; function < cpmBiosFunctionCount; ++function)
  {
    uint16_t vectorAddress = cpmBiosAddress + function * cpmBiosVectorSize;
    uint16_t stubAddress = cpmBiosStubAddress + function * cpmBiosStubSize;
    memory[vectorAddress] = 0xC3;
    writeWord(memory, vectorAddress + 1, stubAddress);
    memory[stubAddress] = 0x3E;
    memory[stubAddress + 1] = function;
    memory[stubAddress + 2] = 0xD3;
    memory[stubAddress + 3] = cpmBiosServicePort;
    memory[stubAddress + 4] = 0xC9;
  }

  memset(memory + cpmBiosDphAddress, 0, 16);
  writeWord(memory, cpmBiosDphAddress + 8, cpmBiosDirectoryBufferAddress);
  writeWord(memory, cpmBiosDphAddress + 10, cpmBiosDpbAddress);
  writeWord(memory, cpmBiosDphAddress + 12, cpmBiosCsvAddress);
  writeWord(memory, cpmBiosDphAddress + 14, cpmBiosAlvAddress);
  memcpy(memory + cpmBiosDpbAddress, dpb, sizeof(dpb));
  memset(memory + cpmBiosDirectoryBufferAddress, 0, cpmDiskSectorSize);
  memset(memory + cpmBiosCsvAddress, 0, cpmBiosCsvSize);
  memset(memory + cpmBiosAlvAddress, 0, cpmBiosAlvSize);

  for (uint8_t drive = 1; drive < cpmDiskDriveCount; ++drive)
  {
    uint16_t driveOffset = (uint16_t)(drive - 1) * cpmBiosAdditionalDriveStride;
    uint16_t dphAddress = cpmBiosAdditionalDphAddress + driveOffset;
    uint16_t directoryBufferAddress = dphAddress + cpmBiosAdditionalDirectoryBufferOffset;
    uint16_t csvAddress = dphAddress + cpmBiosAdditionalCsvOffset;
    uint16_t alvAddress = dphAddress + cpmBiosAdditionalAlvOffset;

    memset(memory + dphAddress, 0, 16);
    writeWord(memory, dphAddress + 8, directoryBufferAddress);
    writeWord(memory, dphAddress + 10, cpmBiosDpbAddress);
    writeWord(memory, dphAddress + 12, csvAddress);
    writeWord(memory, dphAddress + 14, alvAddress);
    memset(memory + directoryBufferAddress, 0, cpmDiskSectorSize);
    memset(memory + csvAddress, 0, cpmBiosCsvSize);
    memset(memory + alvAddress, 0, cpmBiosAlvSize);
  }
}

static void installSystem(cpmGuest *guest, bool coldBoot)
{
  uint8_t *memory = guest->cpu.memory;
  if (coldBoot)
  {
    memset(memory, 0, cpmMemorySize);
  }
  else
  {
    memset(memory, 0, 0x0100);
  }

  memcpy(memory + cpmCcpAddress, guest->ccpImage, sizeof(guest->ccpImage));
  memcpy(memory + cpmBdosAddress, guest->bdosImage, sizeof(guest->bdosImage));
  installBios(guest);

  memory[0x0000] = 0xC3;
  writeWord(memory, 0x0001, cpmBiosAddress + cpmBiosVectorSize);
  memory[0x0003] = 0;
  memory[0x0004] = 0;
  memory[0x0005] = 0xC3;
  writeWord(memory, 0x0006, cpmBdosEntryAddress);

  guest->selectedDrive = 0;
  guest->currentTrack = 0;
  guest->currentSector = 0;
  guest->dmaAddress = cpmDmaAddress;
  if (coldBoot)
  {
    guest->keyboardCharacterAvailable = false;
    guest->skipLineFeed = false;
  }
  resetProcessor(guest, cpmCcpAddress);
}

static bool readConsoleCharacter(cpmGuest *guest, uint8_t *character)
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

static bool readDiskRecord(cpmGuest *guest)
{
  z80 *processor = &guest->cpu.processor;
  uint8_t record[cpmDiskSectorSize];
  if (guest->selectedDrive >= cpmDiskDriveCount || guest->currentTrack >= cpmDiskTracks ||
      guest->currentSector >= cpmDiskSectorsPerTrack || guest->dmaAddress > cpmMemorySize - cpmDiskSectorSize ||
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

static bool writeDiskRecord(cpmGuest *guest)
{
  z80 *processor = &guest->cpu.processor;
  if (guest->selectedDrive >= cpmDiskDriveCount || guest->currentTrack >= cpmDiskTracks ||
      guest->currentSector >= cpmDiskSectorsPerTrack || guest->dmaAddress > cpmMemorySize - cpmDiskSectorSize ||
      !guest->host.diskWriteRecord(guest->host.context, guest->selectedDrive, guest->currentTrack,
                                  guest->currentSector, guest->cpu.memory + guest->dmaAddress))
  {
    processor->a = 1;
    return false;
  }
  processor->a = 0;
  return true;
}

static void serviceBios(cpmGuest *guest, uint8_t function)
{
  z80 *processor = &guest->cpu.processor;
  uint16_t registerBc = getRegisterPair(processor->b, processor->c);

  switch (function)
  {
  case cpmBiosBoot:
    installSystem(guest, true);
    break;
  case cpmBiosWarmBoot:
    installSystem(guest, false);
    break;
  case cpmBiosConst:
    if (!guest->keyboardCharacterAvailable)
    {
      guest->keyboardCharacterAvailable = readConsoleCharacter(guest, &guest->keyboardCharacter);
    }
    processor->a = guest->keyboardCharacterAvailable ? 0xFF : 0;
    break;
  case cpmBiosConin:
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
      processor->pc = cpmBiosStubAddress + cpmBiosConin * cpmBiosStubSize;
      guest->host.yield(guest->host.context);
    }
    break;
  case cpmBiosConout:
    guest->host.consoleWrite(guest->host.context, processor->c);
    break;
  case cpmBiosList:
  case cpmBiosPunch:
    break;
  case cpmBiosReader:
    processor->a = 0x1A;
    break;
  case cpmBiosHome:
    guest->currentTrack = 0;
    break;
  case cpmBiosSeldsk:
    if (processor->c < cpmDiskDriveCount &&
        guest->host.diskDriveAvailable(guest->host.context, processor->c))
    {
      guest->selectedDrive = processor->c;
      uint16_t dphAddress = processor->c == 0
                                ? cpmBiosDphAddress
                                : cpmBiosAdditionalDphAddress +
                                      (uint16_t)(processor->c - 1) * cpmBiosAdditionalDriveStride;
      setRegisterPair(&processor->h, &processor->l, dphAddress);
    }
    else
    {
      setRegisterPair(&processor->h, &processor->l, 0);
    }
    break;
  case cpmBiosSettrk:
    guest->currentTrack = registerBc;
    break;
  case cpmBiosSetsec:
    guest->currentSector = registerBc;
    break;
  case cpmBiosSetdma:
    guest->dmaAddress = registerBc;
    break;
  case cpmBiosRead:
    readDiskRecord(guest);
    break;
  case cpmBiosWrite:
    writeDiskRecord(guest);
    break;
  case cpmBiosListst:
    processor->a = 0;
    break;
  case cpmBiosSectran:
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
  (void)context;
  (void)port;
  return 0xFF;
}

static void writePort(void *context, uint8_t port, uint8_t value)
{
  cpmGuest *guest = context;
  if (port == cpmBiosServicePort)
  {
    serviceBios(guest, value);
  }
}

bool cpmGuestInitialize(cpmGuest *guest, const cpmHostOps *host, const uint8_t *ccpImage, const uint8_t *bdosImage)
{
  if (guest == NULL || host == NULL || ccpImage == NULL || bdosImage == NULL ||
      host->consoleAvailable == NULL || host->consoleRead == NULL || host->consoleWrite == NULL ||
      host->diskDriveAvailable == NULL || host->diskReadRecord == NULL || host->diskWriteRecord == NULL ||
      host->yield == NULL ||
      guest->initialized || guest->cpu.memory != NULL)
  {
    return false;
  }

  guest->host = *host;
  memcpy(guest->ccpImage, ccpImage, sizeof(guest->ccpImage));
  memcpy(guest->bdosImage, bdosImage, sizeof(guest->bdosImage));
  if (!cpmCpuInitialize(&guest->cpu, readPort, writePort, guest))
  {
    memset(guest, 0, sizeof(*guest));
    return false;
  }
  guest->initialized = true;
  installSystem(guest, true);
  return true;
}

void cpmGuestDestroy(cpmGuest *guest)
{
  if (guest == NULL)
  {
    return;
  }
  cpmCpuDestroy(&guest->cpu);
  memset(guest, 0, sizeof(*guest));
}

bool cpmGuestColdBoot(cpmGuest *guest)
{
  if (guest == NULL || !guest->initialized)
  {
    return false;
  }
  resetProcessor(guest, cpmBiosAddress);
  return true;
}

bool cpmGuestStep(cpmGuest *guest)
{
  if (guest == NULL || !guest->initialized || guest->cpu.processor.halted)
  {
    return false;
  }
  return cpmCpuStep(&guest->cpu);
}

size_t cpmGuestRunFor(cpmGuest *guest, size_t instructionBudget)
{
  if (guest == NULL || !guest->initialized)
  {
    return 0;
  }
  size_t instructions = 0;
  while (instructions < instructionBudget && cpmGuestStep(guest))
  {
    ++instructions;
    if ((instructions & 0x0FFF) == 0)
    {
      guest->host.yield(guest->host.context);
    }
  }
  return instructions;
}
