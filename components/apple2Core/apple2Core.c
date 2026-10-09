#define CHIPS_IMPL
#include "m6502.h"

#include "apple2Core.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

struct apple2Core
{
  m6502_t cpu;
  uint64_t pins;
  uint8_t *ram;
  uint8_t rom[apple2RomSize];
  uint8_t busValue;
  uint8_t keyboardLatch;
  apple2VideoState video;
  bool romLoaded;
};

static uint8_t readAddress(apple2Core *core, uint16_t address)
{
  uint8_t value;
  if (address < apple2RamSize)
  {
    value = core->ram[address];
  }
  else if (address == 0xC000)
  {
    value = core->keyboardLatch;
  }
  else if (address == 0xC010)
  {
    core->keyboardLatch &= 0x7F;
    value = core->keyboardLatch;
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
  if (address < apple2RamSize)
  {
    core->ram[address] = value;
  }
  else if (address == 0xC010)
  {
    core->keyboardLatch &= 0x7F;
  }
  else if (address >= 0xC050 && address <= 0xC057)
  {
    readAddress(core, address);
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
  core->keyboardLatch = 0;
  core->busValue = 0xFF;
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

apple2CoreResult apple2CorePressKey(apple2Core *core, uint8_t character)
{
  if (core == NULL || character > 0x7F)
  {
    return apple2CoreInvalidArgument;
  }
  if (core->keyboardLatch & 0x80)
  {
    return apple2CoreKeyBusy;
  }
  if (character >= 'a' && character <= 'z')
  {
    character = (uint8_t)(character - 'a' + 'A');
  }
  core->keyboardLatch = (uint8_t)(character | 0x80);
  return apple2CoreOk;
}

bool apple2CoreKeyPending(const apple2Core *core)
{
  return core != NULL && (core->keyboardLatch & 0x80) != 0;
}

void apple2CoreGetVideoState(const apple2Core *core, apple2VideoState *state)
{
  if (core != NULL && state != NULL)
  {
    *state = core->video;
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
