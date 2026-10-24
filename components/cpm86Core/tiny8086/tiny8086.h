#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//-- 8086tiny-style CPU core: one flat guest RAM array, 20-bit address wrap,
//-- all host I/O reached through the hooks in tiny8086Config.

enum
{
  tiny8086AddressMask = 0x000FFFFF
};

enum
{
  tiny8086FlagCf = 0x0001,
  tiny8086FlagPf = 0x0004,
  tiny8086FlagAf = 0x0010,
  tiny8086FlagZf = 0x0040,
  tiny8086FlagSf = 0x0080,
  tiny8086FlagTf = 0x0100,
  tiny8086FlagIf = 0x0200,
  tiny8086FlagDf = 0x0400,
  tiny8086FlagOf = 0x0800
};

//-- Register indexes (8086 encoding order).
enum
{
  tiny8086RegAx = 0,
  tiny8086RegCx,
  tiny8086RegDx,
  tiny8086RegBx,
  tiny8086RegSp,
  tiny8086RegBp,
  tiny8086RegSi,
  tiny8086RegDi
};

//-- Segment register indexes (8086 encoding order).
enum
{
  tiny8086SegEs = 0,
  tiny8086SegCs,
  tiny8086SegSs,
  tiny8086SegDs
};

typedef enum
{
  tiny8086Ok,
  tiny8086Halted,
  tiny8086InvalidInstruction,
  tiny8086MemoryFault,
  tiny8086IoFault,
  tiny8086InvalidArgument
} tiny8086Result;

typedef struct tiny8086 tiny8086;

//-- Hook: byte port access. Return false for an unsupported port (-> tiny8086IoFault).
typedef bool (*tiny8086PortReadHook)(void* context, uint16_t port, uint8_t* value);
typedef bool (*tiny8086PortWriteHook)(void* context, uint16_t port, uint8_t value);

//-- Hook: called for every INT before the IVT dispatch.
//-- Return true when the host handled the call (CPU continues after the INT),
//-- false to let the CPU dispatch through the guest interrupt vector table.
typedef bool (*tiny8086IntHook)(void* context, tiny8086* cpu, uint8_t vector);

typedef struct
{
  uint8_t* memory;
  uint32_t memorySize;
  tiny8086PortReadHook portRead;
  tiny8086PortWriteHook portWrite;
  tiny8086IntHook intHook;
  void* hookContext;
} tiny8086Config;

struct tiny8086
{
  uint16_t reg[8];
  uint16_t seg[4];
  uint16_t ip;
  uint16_t flags;
  uint8_t* memory;
  uint32_t memorySize;
  tiny8086PortReadHook portRead;
  tiny8086PortWriteHook portWrite;
  tiny8086IntHook intHook;
  void* hookContext;
  uint64_t instructionCount;
  tiny8086Result fault;
  bool halted;
};

tiny8086Result tiny8086Init(tiny8086* cpu, const tiny8086Config* config);
void tiny8086Reset(tiny8086* cpu);
tiny8086Result tiny8086Step(tiny8086* cpu);
tiny8086Result tiny8086Run(tiny8086* cpu, size_t instructionBudget, size_t* instructionsExecuted);

//-- Checked guest memory access for hooks (BIOS DMA etc.). Never hand out host pointers.
bool tiny8086ReadMemory(const tiny8086* cpu, uint32_t address, void* data, size_t length);
bool tiny8086WriteMemory(tiny8086* cpu, uint32_t address, const void* data, size_t length);
