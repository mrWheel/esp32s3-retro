#include "cpm86Core.h"
#include "emu-mem-io.h"
#include "emu-proc.h"
#include "emu-int.h"
#include "op-class.h"
#include "op-exec.h"
#include "op-id.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

struct cpm86Core
{
  uint8_t *memory;
  size_t ramSize;
  hostExchange *exchange;
  cpm86PortRead portRead;
  cpm86PortWrite portWrite;
  void *portContext;
  hostReadMilliseconds readMilliseconds;
  void *clockContext;
  uint32_t millisecondsSnapshot;
  op_desc_t instruction;
  uint16_t decodedSegment;
  uint16_t decodedOffset;
  uint16_t nextOffset;
  uint64_t instructionCount;
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
  bool decoded;
  bool halted;
};

static cpm86Core *activeCore;
int info_level;
int _int_cpu;
byte_t _break_int_flag;
int_num_hand_t _int_tab[] = {{0, NULL}};
static bool ioFault;

enum
{
  cpm86TimerCapabilityPort = 0x00F0,
  cpm86TimerLowPort = 0x00F1,
  cpm86TimerHighPort = 0x00F4,
  cpm86TimerCapability = 0x00B1
};

static bool isTimerPort(word_t port)
{
  return port >= cpm86TimerCapabilityPort && port <= cpm86TimerHighPort;
}

static void resetDecodeState(cpm86Core *core)
{
  core->instructionCount = 0;
  core->decoded = false;
  core->decodedSegment = 0;
  core->decodedOffset = 0;
  core->nextOffset = 0;
  memset(core->trace, 0, sizeof(core->trace));
  core->traceCount = 0;
  core->headCount = 0;
  core->traceNext = 0;
  core->halted = false;
  core->expectedValid = false;
  op_code_base = core->memory;
  op_code_null = 0;
  rep_reset();
  seg_reset();
}

static cpm86CoreResult validateRange(uint32_t address, size_t length, size_t ramSize)
{
  if (address > ramSize || length > ramSize - address)
  {
    return cpm86CoreMemoryRange;
  }
  return cpm86CoreOk;
}

static bool resetExchange(cpm86Core *core)
{
  if (core->exchange == NULL)
  {
    return true;
  }
  char root[sizeof(core->exchange->root)];
  memcpy(root, core->exchange->root, sizeof(root));
  root[sizeof(root) - 1] = '\0';
  hostExchangeClose(core->exchange);
  return hostExchangeInitialize(core->exchange, root);
}

cpm86CoreResult cpm86CoreCreate(cpm86Core **core, hostExchange *exchange, const cpm86CoreConfig *config)
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
  if (activeCore != NULL)
  {
    return cpm86CoreBusy;
  }

  cpm86Core *created = calloc(1, sizeof(*created));
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

  created->exchange = exchange;
  activeCore = created;
  mem_stat = created->memory;
  mem_set_size(created->ramSize);
  *core = created;
  if (check_exec() != 0)
  {
    cpm86CoreDestroy(created);
    *core = NULL;
    return cpm86CoreInvalidInstruction;
  }
  cpm86CoreResult result = cpm86CoreReset(created);
  if (result != cpm86CoreOk)
  {
    cpm86CoreDestroy(created);
    *core = NULL;
  }
  return result;
}

void cpm86CoreDestroy(cpm86Core *core)
{
  if (core == NULL)
  {
    return;
  }
  if (core == activeCore)
  {
    resetExchange(core);
    activeCore = NULL;
    mem_stat = NULL;
    mem_set_size(0);
  }
#ifdef ESP_PLATFORM
  heap_caps_free(core->memory);
#else
  free(core->memory);
#endif
  free(core);
}

cpm86CoreResult cpm86CoreReset(cpm86Core *core)
{
  if (core == NULL || core != activeCore || core->memory == NULL)
  {
    return cpm86CoreInvalidState;
  }
  if (!resetExchange(core))
  {
    return cpm86CoreInvalidState;
  }
  mem_io_reset();
  proc_reset();
  reg16_set(REG_IP, 0);
  reg16_set(REG_SP, 0xFFFE);
  reg16_set(REG_FL, 0x0200);
  seg_set(SEG_CS, 0);
  seg_set(SEG_SS, 0);
  seg_set(SEG_DS, 0);
  seg_set(SEG_ES, 0);
  _int_cpu = 0;
  _break_int_flag = 0;
  ioFault = false;
  core->millisecondsSnapshot = 0;
  resetDecodeState(core);
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreSetEntry(cpm86Core *core, uint16_t segment, uint16_t offset)
{
  if (core == NULL || core != activeCore || core->halted)
  {
    return cpm86CoreInvalidState;
  }
  seg_set(SEG_CS, segment);
  reg16_set(REG_IP, offset);
  core->decoded = false;
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreLoad(cpm86Core *core, uint32_t address, const void *data, size_t length)
{
  if (core == NULL || core != activeCore || (length > 0 && data == NULL))
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

cpm86CoreResult cpm86CoreRead(cpm86Core *core, uint32_t address, void *data, size_t length)
{
  if (core == NULL || core != activeCore || (length > 0 && data == NULL))
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

cpm86CoreResult cpm86CoreWrite(cpm86Core *core, uint32_t address, const void *data, size_t length)
{
  return cpm86CoreLoad(core, address, data, length);
}

cpm86CoreResult cpm86CoreSetExecuteGuard(cpm86Core *core, uint32_t startAddress, uint32_t endAddress)
{
  if (core == NULL || endAddress < startAddress)
  {
    return cpm86CoreInvalidArgument;
  }
  core->guardStart = startAddress;
  core->guardEnd = endAddress;
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreStep(cpm86Core *core)
{
  if (core == NULL || core != activeCore)
  {
    return cpm86CoreInvalidState;
  }
  if (core->halted)
  {
    return cpm86CoreHalted;
  }

  op_code_seg = seg_get(SEG_CS);
  op_code_off = reg16_get(REG_IP);
  mem_fault = 0;
  uint32_t fetchPhysical = (((uint32_t)op_code_seg << 4) + op_code_off) & 0x000FFFFF;
  if (core->guardEnd > core->guardStart && fetchPhysical >= core->guardStart && fetchPhysical < core->guardEnd)
  {
    return cpm86CoreInvalidInstruction;
  }
  if (!core->decoded || op_code_seg != core->decodedSegment || op_code_off != core->decodedOffset)
  {
    memset(&core->instruction, 0, sizeof(core->instruction));
    int decodeResult = op_decode(&core->instruction);
    uint32_t tracePhysical = (((uint32_t)op_code_seg << 4) + op_code_off) & 0x000FFFFF;
    bool zeroOpcode = tracePhysical + 1 < core->ramSize && core->memory[tracePhysical] == 0 &&
                      core->memory[tracePhysical + 1] == 0;
    size_t previousIndex = (core->traceNext + cpm86CoreTraceDepth - 1) % cpm86CoreTraceDepth;
    //-- Collapse runs of 00 00 so the trace keeps the code that led into them.
    bool collapse = zeroOpcode && core->traceCount > 0 && strcmp(core->trace[previousIndex].instruction, "00 00") == 0;
    //-- Keep only control-flow changes so the trace spans the path that led here.
    bool sequential = core->expectedValid && op_code_seg == core->expectedSegment &&
                      reg16_get(REG_IP) == core->expectedOffset;
    if (!collapse && !sequential)
    {
      cpm86CoreTraceEntry *traceEntry = &core->trace[core->traceNext];
      traceEntry->segment = op_code_seg;
      traceEntry->offset = reg16_get(REG_IP);
      traceEntry->stackSegment = seg_get(SEG_SS);
      traceEntry->stackPointer = reg16_get(REG_SP);
      traceEntry->accumulator = reg16_get(REG_AX);
      snprintf(traceEntry->instruction, sizeof(traceEntry->instruction), "%s",
               zeroOpcode ? "00 00" : op_code_str);
      if (core->headCount < cpm86CoreHeadDepth)
      {
        core->head[core->headCount++] = *traceEntry;
      }
      core->traceNext = (core->traceNext + 1) % cpm86CoreTraceDepth;
      if (core->traceCount < cpm86CoreTraceDepth)
      {
        ++core->traceCount;
      }
    }
    core->expectedSegment = op_code_seg;
    core->expectedOffset = op_code_off;
    core->expectedValid = decodeResult == 0;
    if (decodeResult != 0 || op_code_null)
    {
      return mem_fault ? cpm86CoreMemoryFault : cpm86CoreInvalidInstruction;
    }
    core->decodedSegment = op_code_seg;
    core->decodedOffset = reg16_get(REG_IP);
    core->nextOffset = op_code_off;
    core->decoded = true;
  }
  else
  {
    op_code_off = core->nextOffset;
  }

  reg16_set(REG_IP, op_code_off);
  ioFault = false;
  if (op_exec(&core->instruction) != 0)
  {
    if (mem_fault)
    {
      return cpm86CoreMemoryFault;
    }
    return ioFault ? cpm86CoreIoError : cpm86CoreInvalidInstruction;
  }
  if (mem_fault)
  {
    return cpm86CoreMemoryFault;
  }
  ++core->instructionCount;

  if (core->instruction.op_id == OP_HLT)
  {
    core->halted = true;
    return cpm86CoreHalted;
  }
  if (rep_active())
  {
    reg16_set(REG_IP, core->decodedOffset);
  }
  else
  {
    seg_reset();
  }
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreRun(cpm86Core *core, size_t instructionBudget, size_t *instructionsExecuted)
{
  if (instructionsExecuted != NULL)
  {
    *instructionsExecuted = 0;
  }
  if (core == NULL || core != activeCore || instructionBudget == 0)
  {
    return cpm86CoreInvalidArgument;
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
    if (result == cpm86CoreHalted && instructionsExecuted != NULL)
    {
      ++*instructionsExecuted;
    }
    return result;
  }
  return cpm86CoreOk;
}

uint64_t cpm86CoreInstructionCount(const cpm86Core *core)
{
  return core != NULL ? core->instructionCount : 0;
}

cpm86CoreResult cpm86CoreGetProgramCounter(const cpm86Core *core, uint16_t *segment, uint16_t *offset)
{
  if (core == NULL || segment == NULL || offset == NULL)
  {
    return cpm86CoreInvalidArgument;
  }
  if (core != activeCore)
  {
    return cpm86CoreInvalidState;
  }
  *segment = seg_get(SEG_CS);
  *offset = reg16_get(REG_IP);
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreGetRecentTrace(const cpm86Core *core, cpm86CoreTraceEntry *entries, size_t capacity,
                                        size_t *count)
{
  if (core == NULL || entries == NULL || count == NULL || capacity == 0)
  {
    return cpm86CoreInvalidArgument;
  }
  if (core != activeCore)
  {
    return cpm86CoreInvalidState;
  }
  size_t entriesToCopy = core->traceCount < capacity ? core->traceCount : capacity;
  size_t oldestIndex = (core->traceNext + cpm86CoreTraceDepth - entriesToCopy) % cpm86CoreTraceDepth;
  for (size_t index = 0; index < entriesToCopy; ++index)
  {
    entries[index] = core->trace[(oldestIndex + index) % cpm86CoreTraceDepth];
  }
  *count = entriesToCopy;
  return cpm86CoreOk;
}

cpm86CoreResult cpm86CoreGetHeadTrace(const cpm86Core *core, cpm86CoreTraceEntry *entries, size_t capacity,
                                      size_t *count)
{
  if (core == NULL || entries == NULL || count == NULL || core != activeCore)
  {
    return cpm86CoreInvalidArgument;
  }
  size_t entriesToCopy = core->headCount < capacity ? core->headCount : capacity;
  memcpy(entries, core->head, entriesToCopy * sizeof(entries[0]));
  *count = entriesToCopy;
  return cpm86CoreOk;
}

byte_t mem_read_byte(addr_t address)
{
  return mem_read_byte_0(address);
}

word_t mem_read_word(addr_t address)
{
  return mem_read_word_0(address);
}

void mem_write_byte(addr_t address, byte_t value, byte_t init)
{
  (void)mem_write_byte_0(address, value, init);
}

void mem_write_word(addr_t address, word_t value, byte_t init)
{
  (void)mem_write_word_0(address, value, init);
}

int io_read_byte(word_t port, byte_t *value)
{
  if (activeCore == NULL || value == NULL)
  {
    ioFault = true;
    return -1;
  }
  if (port == cpm86TimerCapabilityPort)
  {
    *value = activeCore->readMilliseconds == NULL ? 0 : cpm86TimerCapability;
    return 0;
  }
  if (isTimerPort(port))
  {
    if (port == cpm86TimerLowPort)
    {
      activeCore->millisecondsSnapshot = activeCore->readMilliseconds == NULL
                                             ? 0
                                             : activeCore->readMilliseconds(activeCore->clockContext);
    }
    *value = (byte_t)(activeCore->millisecondsSnapshot >> ((port - cpm86TimerLowPort) * 8));
    return 0;
  }
  if (port == 0x00F8 && activeCore->exchange != NULL)
  {
    *value = hostExchangePortInput(activeCore->exchange, (uint8_t)port);
    return 0;
  }
  if (activeCore->portRead != NULL &&
      activeCore->portRead(activeCore->portContext, port, value))
  {
    return 0;
  }
  ioFault = true;
  return -1;
}

int io_write_byte(word_t port, byte_t value)
{
  if (activeCore == NULL)
  {
    ioFault = true;
    return -1;
  }
  if (isTimerPort(port))
  {
    ioFault = true;
    return -1;
  }
  if ((port == 0x00F8 || port == 0x00F9) && activeCore->exchange != NULL)
  {
    hostExchangePortOutput(activeCore->exchange, (uint8_t)port, value);
    return 0;
  }
  if (activeCore->portWrite != NULL &&
      activeCore->portWrite(activeCore->portContext, port, value))
  {
    return 0;
  }
  ioFault = true;
  return -1;
}

int io_read_word(word_t port, word_t *value)
{
  (void)port;
  (void)value;
  ioFault = true;
  return -1;
}

int io_write_word(word_t port, word_t value)
{
  (void)port;
  (void)value;
  ioFault = true;
  return -1;
}

int int_hand(byte_t interrupt)
{
  (void)interrupt;
  return 1;
}
