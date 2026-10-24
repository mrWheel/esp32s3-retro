#include "cpm86Core.h"
#include "tiny8086.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

struct cpm86Core
{
  tiny8086 cpu;
  uint8_t* memory;
  size_t ramSize;
  cpm86PortRead portRead;
  cpm86PortWrite portWrite;
  void* portContext;
  hostReadMilliseconds readMilliseconds;
  void* clockContext;
  uint32_t millisecondsSnapshot;
};

static cpm86Core* activeCore;

enum
{
  cpm86TimerCapabilityPort = 0x00F0,
  cpm86TimerLowPort = 0x00F1,
  cpm86TimerHighPort = 0x00F4,
  cpm86TimerCapability = 0x00B1
};

static bool isTimerPort(uint16_t port)
{
  return port >= cpm86TimerCapabilityPort && port <= cpm86TimerHighPort;
}

static cpm86CoreResult validateRange(uint32_t address, size_t length, size_t ramSize)
{
  if (address > ramSize || length > ramSize - address)
  {
    return cpm86CoreMemoryRange;
  }
  return cpm86CoreOk;
}

static bool portReadHook(void* context, uint16_t port, uint8_t* value)
{
  cpm86Core* core = context;
  if (port == cpm86TimerCapabilityPort)
  {
    *value = core->readMilliseconds == NULL ? 0 : cpm86TimerCapability;
    return true;
  }
  if (isTimerPort(port))
  {
    if (port == cpm86TimerLowPort)
    {
      core->millisecondsSnapshot =
          core->readMilliseconds == NULL ? 0 : core->readMilliseconds(core->clockContext);
    }
    *value = (uint8_t)(core->millisecondsSnapshot >> ((port - cpm86TimerLowPort) * 8));
    return true;
  }
  return core->portRead != NULL && core->portRead(core->portContext, port, value);
}

static bool portWriteHook(void* context, uint16_t port, uint8_t value)
{
  cpm86Core* core = context;
  if (isTimerPort(port))
  {
    return false;
  }
  return core->portWrite != NULL && core->portWrite(core->portContext, port, value);
}

cpm86CoreResult cpm86CoreCreate(cpm86Core** core, const cpm86CoreConfig* config)
{
  if (core == NULL)
  {
    return cpm86CoreInvalidArgument;
  }
  *core = NULL;
  if (config == NULL || config->ramSize == 0 || config->ramSize > cpm86AddressSpaceSize)
  {
    return cpm86CoreInvalidArgument;
  }
  //-- Keep the one-active-machine contract of the previous core.
  if (activeCore != NULL)
  {
    return cpm86CoreBusy;
  }

  cpm86Core* created = calloc(1, sizeof(*created));
  if (created == NULL)
  {
    return cpm86CoreNoMemory;
  }
  created->ramSize = config->ramSize;
  created->portRead = config->portRead;
  created->portWrite = config->portWrite;
  created->portContext = config->portContext;
  created->readMilliseconds = config->readMilliseconds;
  created->clockContext = config->clockContext;
#ifdef ESP_PLATFORM
  created->memory = heap_caps_malloc(created->ramSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (created->memory == NULL)
  {
    created->memory = heap_caps_malloc(created->ramSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
#else
  created->memory = malloc(created->ramSize);
#endif
  if (created->memory == NULL)
  {
    free(created);
    return cpm86CoreNoMemory;
  }

  const tiny8086Config cpuConfig = {
      .memory = created->memory,
      .memorySize = (uint32_t)created->ramSize,
      .portRead = portReadHook,
      .portWrite = portWriteHook,
      .intHook = NULL,
      .hookContext = created,
  };
  if (tiny8086Init(&created->cpu, &cpuConfig) != tiny8086Ok)
  {
    cpm86CoreDestroy(created);
    return cpm86CoreInvalidArgument;
  }
  activeCore = created;
  *core = created;
  cpm86CoreResult result = cpm86CoreReset(created);
  if (result != cpm86CoreOk)
  {
    cpm86CoreDestroy(created);
    *core = NULL;
  }
  return result;
}

void cpm86CoreDestroy(cpm86Core* core)
{
  if (core == NULL)
  {
    return;
  }
  if (core == activeCore)
  {
    activeCore = NULL;
  }
#ifdef ESP_PLATFORM
  heap_caps_free(core->memory);
#else
  free(core->memory);
#endif
  free(core);
}

cpm86CoreResult cpm86CoreReset(cpm86Core* core)
{
  if (core == NULL || core->memory == NULL)
  {
    return cpm86CoreInvalidState;
  }
  tiny8086Reset(&core->cpu);
  core->millisecondsSnapshot = 0;
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreSetEntry(cpm86Core* core, uint16_t segment, uint16_t offset)
{
  if (core == NULL || core->cpu.halted)
  {
    return cpm86CoreInvalidState;
  }
  core->cpu.seg[tiny8086SegCs] = segment;
  core->cpu.ip = offset;
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreLoad(cpm86Core* core, uint32_t address, const void* data, size_t length)
{
  if (core == NULL || (length > 0 && data == NULL))
  {
    return cpm86CoreInvalidArgument;
  }
  cpm86CoreResult result = validateRange(address, length, core->ramSize);
  if (result != cpm86CoreOk)
  {
    return result;
  }
  if (length > 0)
  {
    memcpy(core->memory + address, data, length);
  }
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreRead(cpm86Core* core, uint32_t address, void* data, size_t length)
{
  if (core == NULL || (length > 0 && data == NULL))
  {
    return cpm86CoreInvalidArgument;
  }
  cpm86CoreResult result = validateRange(address, length, core->ramSize);
  if (result != cpm86CoreOk)
  {
    return result;
  }
  if (length > 0)
  {
    memcpy(data, core->memory + address, length);
  }
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreWrite(cpm86Core* core, uint32_t address, const void* data, size_t length)
{
  return cpm86CoreLoad(core, address, data, length);
}

static cpm86CoreResult mapResult(tiny8086Result result)
{
  switch (result)
  {
  case tiny8086Ok:
    return cpm86CoreOk;
  case tiny8086Halted:
    return cpm86CoreHalted;
  case tiny8086MemoryFault:
    return cpm86CoreMemoryFault;
  case tiny8086IoFault:
    return cpm86CoreIoError;
  case tiny8086InvalidArgument:
    return cpm86CoreInvalidArgument;
  default:
    return cpm86CoreInvalidInstruction;
  }
}

cpm86CoreResult cpm86CoreStep(cpm86Core* core)
{
  if (core == NULL)
  {
    return cpm86CoreInvalidState;
  }
  if (core->cpu.halted)
  {
    return cpm86CoreHalted;
  }
  return mapResult(tiny8086Step(&core->cpu));
}

cpm86CoreResult cpm86CoreRun(cpm86Core* core, size_t instructionBudget,
                             size_t* instructionsExecuted)
{
  if (instructionsExecuted != NULL)
  {
    *instructionsExecuted = 0;
  }
  if (core == NULL || instructionBudget == 0)
  {
    return cpm86CoreInvalidArgument;
  }
  if (core->cpu.halted)
  {
    return cpm86CoreHalted;
  }
  return mapResult(tiny8086Run(&core->cpu, instructionBudget, instructionsExecuted));
}

uint64_t cpm86CoreInstructionCount(const cpm86Core* core)
{
  return core != NULL ? core->cpu.instructionCount : 0;
}

cpm86CoreResult cpm86CoreGetProgramCounter(const cpm86Core* core, uint16_t* segment,
                                           uint16_t* offset)
{
  if (core == NULL || segment == NULL || offset == NULL)
  {
    return cpm86CoreInvalidArgument;
  }
  *segment = core->cpu.seg[tiny8086SegCs];
  *offset = core->cpu.ip;
  return cpm86CoreOk;
}
