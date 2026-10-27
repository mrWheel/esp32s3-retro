#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hostClock.h"

enum
{
  cpm86AddressSpaceSize = 0x100000
};

typedef struct cpm86Core cpm86Core;

typedef bool (*cpm86PortRead)(void* context, uint16_t port, uint8_t* value);
typedef bool (*cpm86PortWrite)(void* context, uint16_t port, uint8_t value);

typedef struct
{
  size_t ramSize;
  cpm86PortRead portRead;
  cpm86PortWrite portWrite;
  void* portContext;
  hostReadMilliseconds readMilliseconds;
  void* clockContext;
} cpm86CoreConfig;

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

cpm86CoreResult cpm86CoreCreate(cpm86Core** core, const cpm86CoreConfig* config);
void cpm86CoreDestroy(cpm86Core* core);
cpm86CoreResult cpm86CoreReset(cpm86Core* core);
cpm86CoreResult cpm86CoreSetEntry(cpm86Core* core, uint16_t segment, uint16_t offset);
cpm86CoreResult cpm86CoreLoad(cpm86Core* core, uint32_t address, const void* data, size_t length);
cpm86CoreResult cpm86CoreRead(cpm86Core* core, uint32_t address, void* data, size_t length);
cpm86CoreResult cpm86CoreWrite(cpm86Core* core, uint32_t address, const void* data, size_t length);
cpm86CoreResult cpm86CoreStep(cpm86Core* core);
cpm86CoreResult cpm86CoreRun(cpm86Core* core, size_t instructionBudget,
                             size_t* instructionsExecuted);
cpm86CoreResult cpm86CoreGetProgramCounter(const cpm86Core* core, uint16_t* segment,
                                           uint16_t* offset);
uint64_t cpm86CoreInstructionCount(const cpm86Core* core);

//-- Fault context captured at the last stop: register/segment state plus the specific
//-- access (physical address or I/O port) that faulted. startIp is the offset of the
//-- faulting instruction in CS; ip is the post-fault program counter.
typedef struct
{
  uint16_t cs, es, ss, ds;
  uint16_t ax, cx, dx, bx, sp, bp, si, di;
  uint16_t flags;
  uint16_t startIp;
  uint16_t ip;
  uint32_t memAddr;
  bool memWrite;
  uint16_t ioPort;
  uint8_t ioValue;
  bool ioRead;
} cpm86CoreFaultInfo;

cpm86CoreResult cpm86CoreGetFaultInfo(const cpm86Core* core, cpm86CoreFaultInfo* info);
