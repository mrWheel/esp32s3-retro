#define CHIPS_IMPL
#include "m6502.h"

#include "apple2Core.h"
#include "apple2Disk.h"
#include "apple2DiskBootRom.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

enum
{
  apple2VidexRamSize = 2048,
  apple2VidexRamBankSize = 512,
  apple2VidexRegisterCount = 32,
  apple2VidexFirmwareSize = 369,
  apple2LowercaseEchoCycleLimit = 4096,
  apple2MonitorSetvidAddress = 0xFE93,
  apple2MonitorSetvidEnd = 0xFEB0
};

//-- CRTC register values programmed by the slot-3 firmware init ($C800); the boot activation applies the same set.
static const uint8_t videxInitRegisters[16] = {0x62, 0x50, 0x50, 0x28, 0x19, 0x00, 0x18, 0x18,
                                               0x00, 0x0F, 0x20, 0x0F, 0x00, 0x00, 0x00, 0x00};

static const uint8_t videxSlotRom[] = {0x48, 0x20, 0x00, 0xC8, 0x68, 0x4C, 0xB3, 0xC8, 0x4C, 0x00, 0xC8};

//— Project-authored slot firmware: C303 initializes the text card and installs its COUT routine.
static const uint8_t videxFirmware[apple2VidexFirmwareSize] = {
    0xA9, 0x00, 0x8D, 0x06, 0x00, 0x8D, 0x07, 0x00, 0xA9, 0xB3, 0x8D, 0x36, 0x00, 0xA9, 0xC8, 0x8D,
    0x37, 0x00, 0xA9, 0x00, 0x8D, 0xB0, 0xC0, 0xA9, 0x62, 0x8D, 0xB1, 0xC0, 0xA9, 0x01, 0x8D, 0xB0,
    0xC0, 0xA9, 0x50, 0x8D, 0xB1, 0xC0, 0xA9, 0x02, 0x8D, 0xB0, 0xC0, 0xA9, 0x50, 0x8D, 0xB1, 0xC0,
    0xA9, 0x03, 0x8D, 0xB0, 0xC0, 0xA9, 0x28, 0x8D, 0xB1, 0xC0, 0xA9, 0x04, 0x8D, 0xB0, 0xC0, 0xA9,
    0x19, 0x8D, 0xB1, 0xC0, 0xA9, 0x05, 0x8D, 0xB0, 0xC0, 0xA9, 0x00, 0x8D, 0xB1, 0xC0, 0xA9, 0x06,
    0x8D, 0xB0, 0xC0, 0xA9, 0x18, 0x8D, 0xB1, 0xC0, 0xA9, 0x07, 0x8D, 0xB0, 0xC0, 0xA9, 0x18, 0x8D,
    0xB1, 0xC0, 0xA9, 0x08, 0x8D, 0xB0, 0xC0, 0xA9, 0x00, 0x8D, 0xB1, 0xC0, 0xA9, 0x09, 0x8D, 0xB0,
    0xC0, 0xA9, 0x0F, 0x8D, 0xB1, 0xC0, 0xA9, 0x0A, 0x8D, 0xB0, 0xC0, 0xA9, 0x20, 0x8D, 0xB1, 0xC0,
    0xA9, 0x0B, 0x8D, 0xB0, 0xC0, 0xA9, 0x0F, 0x8D, 0xB1, 0xC0, 0xA9, 0x0C, 0x8D, 0xB0, 0xC0, 0xA9,
    0x00, 0x8D, 0xB1, 0xC0, 0xA9, 0x0D, 0x8D, 0xB0, 0xC0, 0xA9, 0x00, 0x8D, 0xB1, 0xC0, 0xA9, 0x0E,
    0x8D, 0xB0, 0xC0, 0xA9, 0x00, 0x8D, 0xB1, 0xC0, 0xA9, 0x0F, 0x8D, 0xB0, 0xC0, 0xA9, 0x00, 0x8D,
    0xB1, 0xC0, 0x60, 0x8D, 0x0A, 0x00, 0x48, 0x8A, 0x48, 0x98, 0x48, 0xA5, 0x0A, 0x29, 0x7F, 0x8D,
    0x0A,
    0x00, 0xC9, 0x0D, 0xD0, 0x15, 0xA9, 0x00, 0x8D, 0x06, 0x00, 0xE6, 0x07, 0xA5, 0x07, 0xC9, 0x18,
    0x90, 0x68, 0xA9, 0x00, 0x8D, 0x07, 0x00, 0x4C, 0x3B, 0xC9, 0xA5, 0x0A, 0xC9, 0x08, 0xD0, 0x09,
    0xA5, 0x06, 0xF0, 0x56, 0xC6, 0x06, 0x4C, 0x3B, 0xC9, 0xA5, 0x06, 0xC9, 0x50, 0x90, 0x12, 0xA9,
    0x00, 0x8D, 0x06, 0x00, 0xE6, 0x07, 0xA5, 0x07, 0xC9, 0x18, 0x90, 0x05, 0xA9, 0x17, 0x8D, 0x07,
    0x00, 0xA5, 0x07, 0xAA, 0xBD, 0x41, 0xC9, 0x18, 0x65, 0x06, 0x85, 0x08, 0xBD, 0x59, 0xC9, 0x69,
    0x00, 0x85, 0x09, 0xA5, 0x09, 0x4A, 0x0A, 0x0A, 0xAA, 0xA9, 0x00, 0x9D, 0xB0, 0xC0, 0xA5, 0x09,
    0x29, 0x01, 0xD0, 0x0A, 0xA4, 0x08, 0xA5, 0x0A, 0x99, 0x00, 0xCC, 0x4C, 0x36, 0xC9, 0xA4, 0x08,
    0xA5, 0x0A, 0x99, 0x00, 0xCD, 0xE6, 0x06, 0x4C, 0x3B, 0xC9, 0x68, 0xA8, 0x68, 0xAA, 0x68, 0x60,
    0x00,
    0x50, 0xA0, 0xF0, 0x40, 0x90, 0xE0, 0x30, 0x80, 0xD0, 0x20, 0x70, 0xC0, 0x10, 0x60, 0xB0, 0x00,
    0x50, 0xA0, 0xF0, 0x40, 0x90, 0xE0, 0x30, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02,
    0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x06, 0x07};

struct apple2Core
{
  m6502_t cpu;
  uint64_t pins;
  uint8_t *ram;
  uint8_t rom[apple2RomSize];
  uint8_t busValue;
  uint8_t keyboardData;
  bool keyboardStrobe;
  bool lowercaseEchoPending;
  uint16_t lowercaseEchoCycles;
  uint8_t videxRam[apple2VidexRamSize];
  uint8_t videxRegisters[apple2VidexRegisterCount];
  uint8_t videxWorkspace[5];
  uint8_t videxSavedWorkspace[5];
  size_t videxRamBank;
  uint8_t videxRegisterAddress;
  apple2VideoState video;
  bool cpuBusAccess;
  bool videxWorkspaceActive;
  bool videxOutputSelected;
  bool lowercaseCharacterRom;
  bool lowercaseKeyboard;
  bool romLoaded;
  bool bootIn80Columns;
  bool setvidActive;
  uint64_t cycles;
  apple2Disk disks[apple2DiskSlotCount];
};

static void scrollVidexScreen(apple2Core *core)
{
  memmove(core->videxRam, &core->videxRam[apple2VidexTextColumns],
          (apple2VidexTextRows - 1) * apple2VidexTextColumns);
  memset(&core->videxRam[(apple2VidexTextRows - 1) * apple2VidexTextColumns], 0x20,
         apple2VidexTextColumns);
}

static bool isVidexFirmwareAccess(const apple2Core *core)
{
  uint16_t programCounter = core->cpu.PC;
  return core->cpuBusAccess && programCounter >= 0xC800 &&
         programCounter < 0xC800 + apple2VidexFirmwareSize;
}

static bool isVidexFirmwareProgramCounter(uint16_t programCounter)
{
  return programCounter >= 0xC800 && programCounter < 0xC800 + apple2VidexFirmwareSize;
}

static void beginVidexWorkspace(apple2Core *core)
{
  if (!core->videxWorkspaceActive)
  {
    memcpy(core->videxSavedWorkspace, &core->ram[0x0006], sizeof(core->videxSavedWorkspace));
    core->videxWorkspaceActive = true;
    //-- HTAB/VTAB (and the monitor) move the cursor through CH/CV; the card follows them like real 80-column firmware.
    if (core->ram[0x0024] < apple2VidexTextColumns)
    {
      core->videxWorkspace[0] = core->ram[0x0024];
    }
    if (core->ram[0x0025] < apple2VidexTextRows)
    {
      core->videxWorkspace[1] = core->ram[0x0025];
    }
  }
}

static void endVidexWorkspace(apple2Core *core)
{
  if (core->videxWorkspaceActive && !isVidexFirmwareProgramCounter(core->cpu.PC))
  {
    memcpy(&core->ram[0x0006], core->videxSavedWorkspace, sizeof(core->videxSavedWorkspace));
    core->videxWorkspaceActive = false;
    core->ram[0x0024] = core->videxWorkspace[0];
    core->ram[0x0025] = core->videxWorkspace[1];
  }
}

static bool isVidexOutputSelected(const apple2Core *core)
{
  return core->videxOutputSelected;
}

//-- True while a two-byte vector update is half done, i.e. one byte belongs to a known vector.
static bool isTransientOutputVector(uint16_t vector)
{
  static const uint16_t knownVectors[] = {0xFDF0, 0xC8B3, 0x9EBD};
  for (size_t index = 0; index < sizeof(knownVectors) / sizeof(knownVectors[0]); ++index)
  {
    if ((vector & 0xFF) == (knownVectors[index] & 0xFF) || (vector >> 8) == (knownVectors[index] >> 8))
    {
      return true;
    }
  }
  return false;
}

//-- Apple DOS 3.3 (48K) swaps the output vector for its own hook ($9EBD) and keeps the real one at $AA53/$AA54.
//-- Half-written vectors are ignored so a hook swap never looks like a switch between the two screens.
static bool updateVidexOutputSelection(apple2Core *core)
{
  uint16_t vector = (uint16_t)(core->ram[0x0036] | ((uint16_t)core->ram[0x0037] << 8));
  bool dosHoldsVidex = core->ram[0xAA53] == 0xB3 && core->ram[0xAA54] == 0xC8;
  bool selected = core->videxOutputSelected;
  if (vector == 0xC8B3 || (vector == 0x9EBD && dosHoldsVidex))
  {
    selected = true;
  }
  else if (vector == 0xFDF0 || vector == 0x9EBD || !isTransientOutputVector(vector))
  {
    selected = false;
  }
  return selected;
}

static void clearSelectedDisplay(apple2Core *core, bool videxSelected)
{
  if (videxSelected)
  {
    memset(core->videxRam, 0x20, sizeof(core->videxRam));
    memset(core->videxWorkspace, 0, sizeof(core->videxWorkspace));
    return;
  }

  //-- Back on the motherboard screen the cursor starts at home with a matching base address.
  core->ram[0x0024] = 0;
  core->ram[0x0025] = 0;
  core->ram[0x0028] = 0x00;
  core->ram[0x0029] = 0x04;

  for (size_t row = 0; row < apple2TextRows; ++row)
  {
    for (size_t column = 0; column < apple2TextColumns; ++column)
    {
      uint16_t address;
      if (apple2CoreTextAddress(core->video.page2, row, column, &address))
      {
        core->ram[address] = 0xA0;
      }
    }
  }
}

//-- Equivalent of PR#3 done by the host at reset: same CRTC registers, output vector and cleared screen.
static void activateVidexAtBoot(apple2Core *core)
{
  memcpy(core->videxRegisters, videxInitRegisters, sizeof(videxInitRegisters));
  core->video.videxTextMode = core->videxRegisters[1] == apple2VidexTextColumns &&
                              core->videxRegisters[6] == apple2VidexTextRows;
  core->ram[0x0036] = 0xB3;
  core->ram[0x0037] = 0xC8;
  core->videxOutputSelected = true;
  clearSelectedDisplay(core, true);
  core->ram[0x0024] = 0;
  core->ram[0x0025] = 0;
}

static uint8_t readVidexIo(apple2Core *core, uint16_t address)
{
  uint8_t offset = (uint8_t)(address - 0xC0B0);
  core->videxRamBank = ((size_t)(offset >> 2) & 3U) * apple2VidexRamBankSize;
  return offset == 1 ? core->videxRegisters[core->videxRegisterAddress] : core->busValue;
}

static void writeVidexIo(apple2Core *core, uint16_t address, uint8_t value)
{
  uint8_t offset = (uint8_t)(address - 0xC0B0);
  core->videxRamBank = ((size_t)(offset >> 2) & 3U) * apple2VidexRamBankSize;
  if (offset == 0)
  {
    core->videxRegisterAddress = value & 0x1F;
  }
  else if (offset == 1)
  {
    core->videxRegisters[core->videxRegisterAddress] = value;
    core->video.videxTextMode = core->videxRegisters[1] == apple2VidexTextColumns &&
                                core->videxRegisters[6] == apple2VidexTextRows;
  }
}

static void clearKeyboardStrobe(apple2Core *core)
{
  bool keyWasReady = core->keyboardStrobe;
  core->keyboardStrobe = false;
  if (core->cpuBusAccess && keyWasReady && core->lowercaseKeyboard &&
      core->keyboardData >= 'a' && core->keyboardData <= 'z')
  {
    core->lowercaseEchoPending = true;
    core->lowercaseEchoCycles = apple2LowercaseEchoCycleLimit;
  }
}

//-- Limit the case correction to the screen echo immediately following key acknowledgement.
static uint8_t preserveLowercaseEcho(apple2Core *core, uint8_t value, bool videx)
{
  if (!core->cpuBusAccess || !core->lowercaseEchoPending)
  {
    return value;
  }
  uint8_t uppercase = (uint8_t)(core->keyboardData - 'a' + 'A');
  uint8_t expected = videx ? uppercase : (uint8_t)(uppercase | 0x80);
  if (value != expected)
  {
    return value;
  }
  core->lowercaseEchoPending = false;
  core->lowercaseEchoCycles = 0;
  return videx ? core->keyboardData : (uint8_t)(core->keyboardData - 'a' + 0xE1);
}

//-- The controller that answers the I/O range $C0n0..$C0nF (slot n), or NULL when no drive is attached there.
static apple2Disk *diskForIo(apple2Core *core, uint16_t address)
{
  if (address < 0xC080 + apple2DiskFirstSlot * 0x10 || address >= 0xC080 + (apple2DiskLastSlot + 1) * 0x10)
  {
    return NULL;
  }
  apple2Disk *disk = &core->disks[((address - 0xC080) >> 4) - apple2DiskFirstSlot];
  return apple2DiskIsAttached(disk) ? disk : NULL;
}

//-- The controller whose boot ROM is mapped at $Cn00..$CnFF (slot n), or NULL when no drive is attached there.
static apple2Disk *diskForRom(apple2Core *core, uint16_t address)
{
  if (address < 0xC000 + apple2DiskFirstSlot * 0x100 || address >= 0xC000 + (apple2DiskLastSlot + 1) * 0x100)
  {
    return NULL;
  }
  apple2Disk *disk = &core->disks[((address - 0xC000) >> 8) - apple2DiskFirstSlot];
  return apple2DiskIsAttached(disk) ? disk : NULL;
}

static uint8_t readAddress(apple2Core *core, uint16_t address)
{
  uint8_t value;
  apple2Disk *disk;
  if (address >= 0x0006 && address <= 0x000A && isVidexFirmwareAccess(core))
  {
    beginVidexWorkspace(core);
    value = core->videxWorkspace[address - 0x0006];
  }
  else if (address < apple2RamSize)
  {
    value = core->ram[address];
  }
  else if (address == 0xC000)
  {
    value = core->keyboardData | (core->keyboardStrobe ? 0x80 : 0x00);
  }
  else if (address == 0xC010)
  {
    clearKeyboardStrobe(core);
    value = core->keyboardData;
  }
  else if (address == 0xC063)
  {
    value = 0x00;
  }
  else if ((disk = diskForIo(core, address)) != NULL)
  {
    value = apple2DiskAccess(disk, (uint8_t)(address & 0x0F), false, core->cycles, core->busValue);
  }
  else if ((disk = diskForRom(core, address)) != NULL)
  {
    value = apple2DiskBootRoms[(address >> 8) - 0xC0 - apple2DiskFirstSlot][address & 0xFF];
  }
  else if (address >= 0xC0B0 && address <= 0xC0BF)
  {
    value = readVidexIo(core, address);
  }
  else if (address >= 0xC050 && address <= 0xC057)
  {
    switch (address)
    {
    case 0xC050:
      core->video.textMode = false;
      break;
    case 0xC051:
      core->video.textMode = true;
      break;
    case 0xC052:
      core->video.mixedMode = false;
      break;
    case 0xC053:
      core->video.mixedMode = true;
      break;
    case 0xC054:
      core->video.page2 = false;
      break;
    case 0xC055:
      core->video.page2 = true;
      break;
    case 0xC056:
      core->video.highResolution = false;
      break;
    case 0xC057:
      core->video.highResolution = true;
      break;
    default:
      break;
    }
    value = core->busValue;
  }
  else if (address >= 0xC300 && address <= 0xC3FF)
  {
    size_t offset = address - 0xC300;
    value = offset < sizeof(videxSlotRom) ? videxSlotRom[offset] : 0xFF;
  }
  else if (address >= 0xC800 && address <= 0xCBFF)
  {
    size_t offset = address - 0xC800;
    value = offset < sizeof(videxFirmware) ? videxFirmware[offset] : 0xFF;
  }
  else if (address >= 0xCC00 && address <= 0xCDFF)
  {
    value = core->videxRam[core->videxRamBank + (address - 0xCC00)];
  }
  else if (address >= 0xD000)
  {
    value = core->rom[address - 0xD000];
  }
  else
  {
    value = core->busValue;
  }
  core->busValue = value;
  return value;
}

static void writeAddress(apple2Core *core, uint16_t address, uint8_t value)
{
  apple2Disk *disk;
  if (address >= 0x0006 && address <= 0x000A && isVidexFirmwareAccess(core))
  {
    beginVidexWorkspace(core);
    if (address == 0x0007 && core->videxWorkspace[1] == apple2VidexTextRows &&
        (value == 0 || value == apple2VidexTextRows - 1) && core->video.videxTextMode &&
        isVidexOutputSelected(core))
    {
      scrollVidexScreen(core);
      value = apple2VidexTextRows - 1;
    }
    core->videxWorkspace[address - 0x0006] = value;
  }
  else if (address < apple2RamSize)
  {
    //-- While the monitor SETVID routine runs the default video output is the 80-column card, not the 40-column screen.
    if (core->setvidActive && (address == 0x0036 || address == 0x0037))
    {
      value = address == 0x0036 ? 0xB3 : 0xC8;
    }
    bool isOutputVectorAddress = address == 0x0036 || address == 0x0037 || address == 0xAA53 || address == 0xAA54;
    if (address >= 0x0400 && address <= 0x0BFF)
    {
      value = preserveLowercaseEcho(core, value, false);
    }
    core->ram[address] = value;
    if (isOutputVectorAddress)
    {
      bool videxSelected = updateVidexOutputSelection(core);
      if (videxSelected != core->videxOutputSelected)
      {
        core->videxOutputSelected = videxSelected;
        clearSelectedDisplay(core, videxSelected);
      }
    }
  }
  else if (address == 0xC010)
  {
    clearKeyboardStrobe(core);
  }
  else if ((disk = diskForIo(core, address)) != NULL)
  {
    apple2DiskAccess(disk, (uint8_t)(address & 0x0F), true, core->cycles, value);
  }
  else if (address >= 0xC0B0 && address <= 0xC0BF)
  {
    writeVidexIo(core, address, value);
  }
  else if (address >= 0xC050 && address <= 0xC057)
  {
    readAddress(core, address);
  }
  else if (address >= 0xCC00 && address <= 0xCDFF)
  {
    value = preserveLowercaseEcho(core, value, true);
    core->videxRam[core->videxRamBank + (address - 0xCC00)] = value;
  }
  core->busValue = value;
}

apple2CoreResult apple2CoreCreate(apple2Core **core)
{
  if (core == NULL)
  {
    return apple2CoreInvalidArgument;
  }
  *core = NULL;
  apple2Core *created = calloc(1, sizeof(*created));
  if (created == NULL)
  {
    return apple2CoreNoMemory;
  }
#ifdef ESP_PLATFORM
  created->ram = heap_caps_calloc(apple2RamSize, sizeof(*created->ram), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (created->ram == NULL)
  {
    created->ram = heap_caps_calloc(apple2RamSize, sizeof(*created->ram), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
#else
  created->ram = calloc(apple2RamSize, sizeof(*created->ram));
#endif
  if (created->ram == NULL)
  {
    free(created);
    return apple2CoreNoMemory;
  }
  created->busValue = 0xFF;
  for (size_t slot = 0; slot < apple2DiskSlotCount; ++slot)
  {
    apple2DiskInitialize(&created->disks[slot]);
  }
  *core = created;
  return apple2CoreOk;
}

void apple2CoreDestroy(apple2Core *core)
{
  if (core == NULL)
  {
    return;
  }
  free(core->ram);
  free(core);
}

apple2CoreResult apple2CoreLoadRom(apple2Core *core, const uint8_t *rom, size_t size)
{
  if (core == NULL || rom == NULL || size != apple2RomSize)
  {
    return apple2CoreInvalidArgument;
  }
  uint16_t resetAddress = (uint16_t)(rom[0x2FFC] | ((uint16_t)rom[0x2FFD] << 8));
  if (resetAddress < 0xD000)
  {
    return apple2CoreInvalidRom;
  }
  memcpy(core->rom, rom, sizeof(core->rom));
  core->romLoaded = true;
  return apple2CoreReset(core);
}

apple2CoreResult apple2CoreReset(apple2Core *core)
{
  if (core == NULL || !core->romLoaded)
  {
    return apple2CoreInvalidArgument;
  }
  core->video = (apple2VideoState){.textMode = true};
  memset(core->videxRegisters, 0, sizeof(core->videxRegisters));
  memset(core->videxWorkspace, 0, sizeof(core->videxWorkspace));
  memset(core->videxSavedWorkspace, 0, sizeof(core->videxSavedWorkspace));
  core->videxWorkspaceActive = false;
  core->videxOutputSelected = false;
  core->videxRamBank = 0;
  core->videxRegisterAddress = 0;
  core->keyboardData = 0;
  core->keyboardStrobe = false;
  core->lowercaseEchoPending = false;
  core->lowercaseEchoCycles = 0;
  core->setvidActive = false;
  core->busValue = 0xFF;
  for (size_t slot = 0; slot < apple2DiskSlotCount; ++slot)
  {
    apple2DiskResetSwitches(&core->disks[slot]);
  }
  if (core->bootIn80Columns)
  {
    activateVidexAtBoot(core);
  }
  core->pins = m6502_init(&core->cpu, &(m6502_desc_t){.bcd_disabled = false});
  return apple2CoreOk;
}

apple2CoreResult apple2CoreRunCycles(apple2Core *core, size_t cycles)
{
  if (core == NULL || !core->romLoaded)
  {
    return apple2CoreInvalidArgument;
  }
  for (size_t index = 0; index < cycles; ++index)
  {
    if (core->lowercaseEchoPending && core->lowercaseEchoCycles > 0 &&
        --core->lowercaseEchoCycles == 0)
    {
      core->lowercaseEchoPending = false;
    }
    core->cycles++;
    core->cpuBusAccess = true;
    core->pins = m6502_tick(&core->cpu, core->pins);
    uint16_t address = M6502_GET_ADDR(core->pins);
    if (core->pins & M6502_RW)
    {
      M6502_SET_DATA(core->pins, readAddress(core, address));
    }
    else
    {
      writeAddress(core, address, M6502_GET_DATA(core->pins));
    }
    //-- Monitor SETVID ($FE93, also called by the DOS boot code) is told apart from PR#n, which enters at OUTPORT ($FE95).
    if ((core->pins & M6502_SYNC) != 0 && core->bootIn80Columns)
    {
      if (address == apple2MonitorSetvidAddress)
      {
        core->setvidActive = true;
      }
      else if (core->setvidActive && (address < apple2MonitorSetvidAddress || address >= apple2MonitorSetvidEnd))
      {
        core->setvidActive = false;
      }
    }
    //-- The monitor HOME routine only knows the 40-column page; clear the card screen when it is selected.
    if ((core->pins & M6502_SYNC) != 0 && address == 0xFC58 && core->videxOutputSelected)
    {
      clearSelectedDisplay(core, true);
      core->ram[0x0024] = 0;
      core->ram[0x0025] = 0;
    }
    endVidexWorkspace(core);
    core->cpuBusAccess = false;
  }
  return apple2CoreOk;
}

apple2CoreResult apple2CoreReadMemory(apple2Core *core, uint16_t address, uint8_t *value)
{
  if (core == NULL || value == NULL)
  {
    return apple2CoreInvalidArgument;
  }
  *value = readAddress(core, address);
  return apple2CoreOk;
}

apple2CoreResult apple2CoreWriteMemory(apple2Core *core, uint16_t address, uint8_t value)
{
  if (core == NULL)
  {
    return apple2CoreInvalidArgument;
  }
  writeAddress(core, address, value);
  return apple2CoreOk;
}

apple2CoreResult apple2CoreSetBootIn80Columns(apple2Core *core, bool enabled)
{
  if (core == NULL)
  {
    return apple2CoreInvalidArgument;
  }
  core->bootIn80Columns = enabled;
  return apple2CoreOk;
}

apple2CoreResult apple2CoreSetCharacterOptions(apple2Core *core, bool lowercaseCharacterRom,
                                               bool lowercaseKeyboard)
{
  if (core == NULL)
  {
    return apple2CoreInvalidArgument;
  }
  core->lowercaseCharacterRom = lowercaseCharacterRom;
  core->lowercaseKeyboard = lowercaseKeyboard;
  return apple2CoreOk;
}

char apple2CoreDecodeTextCharacter(const apple2Core *core, uint8_t value)
{
  if (core != NULL && core->lowercaseCharacterRom && value >= 0xE1 && value <= 0xFA)
  {
    return (char)(value - 0xE1 + 'a');
  }
  uint8_t character = value & 0x3F;
  if (character <= 0x1F)
  {
    return (char)(character + '@');
  }
  if (character == 0x20)
  {
    return ' ';
  }
  return (char)character;
}

apple2CoreResult apple2CorePressKey(apple2Core *core, uint8_t character)
{
  if (core == NULL || character > 0x7F)
  {
    return apple2CoreInvalidArgument;
  }
  if (core->keyboardStrobe)
  {
    return apple2CoreKeyBusy;
  }
  core->lowercaseEchoPending = false;
  core->lowercaseEchoCycles = 0;
  if (!core->lowercaseKeyboard && character >= 'a' && character <= 'z')
  {
    character = (uint8_t)(character - 'a' + 'A');
  }
  core->keyboardData = character;
  core->keyboardStrobe = true;
  return apple2CoreOk;
}

bool apple2CoreKeyPending(const apple2Core *core)
{
  return core != NULL && core->keyboardStrobe;
}

void apple2CoreGetVideoState(const apple2Core *core, apple2VideoState *state)
{
  if (core != NULL && state != NULL)
  {
    *state = core->video;
    state->videxTextMode = core->video.videxTextMode && isVidexOutputSelected(core);
  }
}

bool apple2CoreTextAddress(bool page2, size_t row, size_t column, uint16_t *address)
{
  if (address == NULL || row >= apple2TextRows || column >= apple2TextColumns)
  {
    return false;
  }
  size_t base = page2 ? 0x0800 : 0x0400;
  *address = (uint16_t)(base + ((row & 7U) << 7) + ((row >> 3) * apple2TextColumns) + column);
  return true;
}

apple2CoreResult apple2CoreReadTextCell(const apple2Core *core, bool page2, size_t row, size_t column,
                                        uint8_t *value)
{
  uint16_t address;
  if (core == NULL || value == NULL || !apple2CoreTextAddress(page2, row, column, &address))
  {
    return apple2CoreInvalidArgument;
  }
  *value = core->ram[address];
  return apple2CoreOk;
}

apple2CoreResult apple2CoreReadVidexTextCell(const apple2Core *core, size_t row, size_t column, uint8_t *value)
{
  if (core == NULL || value == NULL || row >= apple2VidexTextRows || column >= apple2VidexTextColumns)
  {
    return apple2CoreInvalidArgument;
  }
  size_t startAddress = (((size_t)core->videxRegisters[12] & 0x3FU) << 8) | core->videxRegisters[13];
  size_t address = (startAddress + row * apple2VidexTextColumns + column) & (apple2VidexRamSize - 1);
  *value = core->videxRam[address];
  return apple2CoreOk;
}

bool apple2CoreGetVidexCursor(const apple2Core *core, size_t *row, size_t *column)
{
  if (core == NULL || row == NULL || column == NULL || !core->video.videxTextMode ||
      !isVidexOutputSelected(core))
  {
    return false;
  }
  *column = core->videxWorkspace[0];
  *row = core->videxWorkspace[1];
  return true;
}

bool apple2CoreGetTextCursor(const apple2Core *core, size_t *row, size_t *column)
{
  if (core == NULL || row == NULL || column == NULL || core->ram[0x0024] >= apple2TextColumns ||
      core->ram[0x0025] >= apple2TextRows)
  {
    return false;
  }
  *column = core->ram[0x0024];
  *row = core->ram[0x0025];
  return true;
}

apple2CoreResult apple2CoreAttachDisk(apple2Core *core, apple2DiskReadSectorFunction readSector, void *context)
{
  return apple2CoreAttachDiskDrive(core, 6, 0, apple2DiskSectorOrderDos, apple2DiskTrackCount, readSector, context);
}

apple2CoreResult apple2CoreDetachDisk(apple2Core *core)
{
  if (core == NULL)
  {
    return apple2CoreInvalidArgument;
  }
  for (size_t slot = 0; slot < apple2DiskSlotCount; ++slot)
  {
    apple2DiskDetach(&core->disks[slot]);
    apple2DiskResetSwitches(&core->disks[slot]);
  }
  return apple2CoreOk;
}

void apple2CoreGetDiskState(const apple2Core *core, apple2DiskState *state)
{
  apple2CoreGetDiskStateForSlot(core, 6, state);
}

apple2CoreResult apple2CoreAttachDiskDrive(apple2Core *core, uint8_t slot, uint8_t drive, apple2DiskSectorOrder order,
                                           uint8_t trackCount, apple2DiskReadSectorFunction readSector,
                                           void *context)
{
  if (core == NULL || slot < apple2DiskFirstSlot || slot > apple2DiskLastSlot ||
      !apple2DiskAttachDrive(&core->disks[slot - apple2DiskFirstSlot], drive, order, trackCount, readSector, context))
  {
    return apple2CoreInvalidArgument;
  }
  return apple2CoreOk;
}

apple2CoreResult apple2CoreDetachDiskDrive(apple2Core *core, uint8_t slot, uint8_t drive)
{
  if (core == NULL || slot < apple2DiskFirstSlot || slot > apple2DiskLastSlot || drive >= apple2DiskDrivesPerSlot)
  {
    return apple2CoreInvalidArgument;
  }
  apple2Disk *disk = &core->disks[slot - apple2DiskFirstSlot];
  apple2DiskDetachDrive(disk, drive);
  if (!apple2DiskIsAttached(disk))
  {
    apple2DiskResetSwitches(disk);
  }
  return apple2CoreOk;
}

bool apple2CoreGetDiskStateForSlot(const apple2Core *core, uint8_t slot, apple2DiskState *state)
{
  if (core == NULL || state == NULL || slot < apple2DiskFirstSlot || slot > apple2DiskLastSlot)
  {
    return false;
  }
  apple2DiskGetState(&core->disks[slot - apple2DiskFirstSlot], state);
  return true;
}
