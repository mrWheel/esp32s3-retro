#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hostClock.h"
#include "hostExchange.h"

enum
{
  cpm86AddressSpaceSize = 0x100000,
  cpm86CoreTraceDepth = 48,
  cpm86CoreHeadDepth = 60,
  cpm86CoreTraceInstructionSize = 20
};

typedef struct cpm86Core cpm86Core;

typedef bool (*cpm86PortRead)(void *context, uint16_t port, uint8_t *value);
typedef bool (*cpm86PortWrite)(void *context, uint16_t port, uint8_t value);

typedef struct
{
  size_t ramSize;
  cpm86PortRead portRead;
  cpm86PortWrite portWrite;
  void *portContext;
  hostReadMilliseconds readMilliseconds;
  void *clockContext;
  bool captureInstructionTrace;
} cpm86CoreConfig;

typedef struct
{
  uint16_t segment;
  uint16_t offset;
  uint16_t stackSegment;
  uint16_t stackPointer;
  uint16_t accumulator;
  char instruction[cpm86CoreTraceInstructionSize];
} cpm86CoreTraceEntry;

typedef enum
{
  cpm86CoreOk,
  cpm86CoreInvalidArgument,
  cpm86CoreBusy,
  cpm86CoreNoMemory,
  cpm86CoreInvalidState,
  cpm86CoreMemoryRange,
  cpm86CoreMemoryFault,
  cpm86CoreInvalidInstruction,
  cpm86CoreIoError,
  cpm86CoreHalted
} cpm86CoreResult;

cpm86CoreResult cpm86CoreCreate(cpm86Core **core, hostExchange *exchange, const cpm86CoreConfig *config);
void cpm86CoreDestroy(cpm86Core *core);
cpm86CoreResult cpm86CoreReset(cpm86Core *core);
cpm86CoreResult cpm86CoreSetEntry(cpm86Core *core, uint16_t segment, uint16_t offset);
cpm86CoreResult cpm86CoreLoad(cpm86Core *core, uint32_t address, const void *data, size_t length);
cpm86CoreResult cpm86CoreRead(cpm86Core *core, uint32_t address, void *data, size_t length);
cpm86CoreResult cpm86CoreWrite(cpm86Core *core, uint32_t address, const void *data, size_t length);
//-- Debug aid: stop with InvalidInstruction when code is fetched from [start, end).
cpm86CoreResult cpm86CoreSetExecuteGuard(cpm86Core *core, uint32_t startAddress, uint32_t endAddress);
cpm86CoreResult cpm86CoreStep(cpm86Core *core);
cpm86CoreResult cpm86CoreRun(cpm86Core *core, size_t instructionBudget, size_t *instructionsExecuted);
cpm86CoreResult cpm86CoreGetProgramCounter(const cpm86Core *core, uint16_t *segment, uint16_t *offset);
cpm86CoreResult cpm86CoreGetRecentTrace(const cpm86Core *core, cpm86CoreTraceEntry *entries, size_t capacity,
                                        size_t *count);
cpm86CoreResult cpm86CoreGetHeadTrace(const cpm86Core *core, cpm86CoreTraceEntry *entries, size_t capacity,
                                      size_t *count);
uint64_t cpm86CoreInstructionCount(const cpm86Core *core);
