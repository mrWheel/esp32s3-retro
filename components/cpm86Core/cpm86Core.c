#include "cpm86Core.h"
#include "tiny8086.h"
#include <stdio.h>
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
  bool captureInstructionTrace;
  cpm86CoreTraceEntry trace[cpm86CoreTraceDepth];
  cpm86CoreTraceEntry head[cpm86CoreHeadDepth];
  size_t headCount;
  size_t traceCount;
  size_t traceNext;
  uint16_t expectedSegment;
  uint16_t expectedOffset;
  bool expectedValid;
  uint32_t guardStart;
  uint32_t guardEnd;
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

static uint32_t physicalAddress(uint16_t segment, uint16_t offset)
{
  return (((uint32_t)segment << 4) + offset) & tiny8086AddressMask;
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

static void resetTraceState(cpm86Core* core)
{
  memset(core->trace, 0, sizeof(core->trace));
  core->traceCount = 0;
  core->headCount = 0;
  core->traceNext = 0;
  core->expectedValid = false;
}

static void recordTrace(cpm86Core* core)
{
  uint16_t segment = core->cpu.seg[tiny8086SegCs];
  uint16_t offset = core->cpu.ip;
  uint32_t physical = physicalAddress(segment, offset);
  bool zeroOpcode = physical + 1 < core->ramSize && core->memory[physical] == 0 &&
                    core->memory[physical + 1] == 0;
  size_t previousIndex = (core->traceNext + cpm86CoreTraceDepth - 1) % cpm86CoreTraceDepth;
  //-- Collapse runs of 00 00 so the trace keeps the code that led into them.
  bool collapse = zeroOpcode && core->traceCount > 0 &&
                  strcmp(core->trace[previousIndex].instruction, "00 00") == 0;
  //-- Keep only control-flow changes so the trace spans the path that led here.
  bool sequential =
      core->expectedValid && segment == core->expectedSegment && offset == core->expectedOffset;
  if (!collapse && !sequential)
  {
    cpm86CoreTraceEntry* entry = &core->trace[core->traceNext];
    entry->segment = segment;
    entry->offset = offset;
    entry->stackSegment = core->cpu.seg[tiny8086SegSs];
    entry->stackPointer = core->cpu.reg[tiny8086RegSp];
    entry->accumulator = core->cpu.reg[tiny8086RegAx];
    //-- No disassembler in the new core: the trace holds the raw opcode bytes.
    entry->instruction[0] = '\0';
    for (size_t index = 0; index < 6 && physical + index < core->ramSize; ++index)
    {
      size_t used = strlen(entry->instruction);
      snprintf(entry->instruction + used, sizeof(entry->instruction) - used, "%s%02X",
               index == 0 ? "" : " ", core->memory[physical + index]);
    }
    if (core->headCount < cpm86CoreHeadDepth)
    {
      core->head[core->headCount++] = *entry;
    }
    core->traceNext = (core->traceNext + 1) % cpm86CoreTraceDepth;
    if (core->traceCount < cpm86CoreTraceDepth)
    {
      ++core->traceCount;
    }
  }
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
  created->captureInstructionTrace = config->captureInstructionTrace;
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
  resetTraceState(core);
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

cpm86CoreResult cpm86CoreSetExecuteGuard(cpm86Core* core, uint32_t startAddress,
                                         uint32_t endAddress)
{
  if (core == NULL || endAddress < startAddress)
  {
    return cpm86CoreInvalidArgument;
  }
  core->guardStart = startAddress;
  core->guardEnd = endAddress;
  return cpm86CoreOk;
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

  uint16_t segment = core->cpu.seg[tiny8086SegCs];
  uint16_t offset = core->cpu.ip;
  uint32_t fetchPhysical = physicalAddress(segment, offset);
  if (core->guardEnd > core->guardStart && fetchPhysical >= core->guardStart &&
      fetchPhysical < core->guardEnd)
  {
    return cpm86CoreInvalidInstruction;
  }
  if (core->captureInstructionTrace)
  {
    recordTrace(core);
  }

  tiny8086Result result = tiny8086Step(&core->cpu);
  if (core->captureInstructionTrace)
  {
    core->expectedSegment = segment;
    core->expectedOffset = core->cpu.ip;
    core->expectedValid = result == tiny8086Ok;
  }
  return mapResult(result);
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
  //-- Fast path: no per-instruction bookkeeping needed, run inside the core loop.
  if (!core->captureInstructionTrace && core->guardEnd <= core->guardStart)
  {
    if (core->cpu.halted)
    {
      return cpm86CoreHalted;
    }
    return mapResult(tiny8086Run(&core->cpu, instructionBudget, instructionsExecuted));
  }
  for (size_t index = 0; index < instructionBudget; ++index)
  {
    cpm86CoreResult result = cpm86CoreStep(core);
    if (result == cpm86CoreOk)
    {
      if (instructionsExecuted != NULL)
      {
        ++*instructionsExecuted;
      }
      continue;
    }
    if (result == cpm86CoreHalted && instructionsExecuted != NULL && core->cpu.halted)
    {
      ++*instructionsExecuted;
    }
    return result;
  }
  return cpm86CoreOk;
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

cpm86CoreResult cpm86CoreGetRecentTrace(const cpm86Core* core, cpm86CoreTraceEntry* entries,
                                        size_t capacity, size_t* count)
{
  if (core == NULL || entries == NULL || count == NULL || capacity == 0)
  {
    return cpm86CoreInvalidArgument;
  }
  size_t entriesToCopy = core->traceCount < capacity ? core->traceCount : capacity;
  size_t oldestIndex =
      (core->traceNext + cpm86CoreTraceDepth - entriesToCopy) % cpm86CoreTraceDepth;
  for (size_t index = 0; index < entriesToCopy; ++index)
  {
    entries[index] = core->trace[(oldestIndex + index) % cpm86CoreTraceDepth];
  }
  *count = entriesToCopy;
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreGetHeadTrace(const cpm86Core* core, cpm86CoreTraceEntry* entries,
                                      size_t capacity, size_t* count)
{
  if (core == NULL || entries == NULL || count == NULL)
  {
    return cpm86CoreInvalidArgument;
  }
  size_t entriesToCopy = core->headCount < capacity ? core->headCount : capacity;
  memcpy(entries, core->head, entriesToCopy * sizeof(entries[0]));
  *count = entriesToCopy;
  return cpm86CoreOk;
}
