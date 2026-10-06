#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "z80.h"

enum
{
  cpm80MemorySize = 65536
};

typedef uint8_t (*cpm80PortInput)(void *context, uint8_t port);
typedef void (*cpm80PortOutput)(void *context, uint8_t port, uint8_t value);

typedef struct
{
  z80 processor;
  uint8_t *memory;
  cpm80PortInput portInput;
  cpm80PortOutput portOutput;
  void *portContext;
  bool initialized;
} cpm80Cpu;

//— Initialize instances with {0}; destroy them before initializing them again.
bool cpm80CpuInitialize(cpm80Cpu *cpu, cpm80PortInput portInput, cpm80PortOutput portOutput, void *portContext);
void cpm80CpuDestroy(cpm80Cpu *cpu);
bool cpm80CpuLoad(cpm80Cpu *cpu, uint16_t address, const uint8_t *data, size_t length);
bool cpm80CpuReadMemory(const cpm80Cpu *cpu, uint16_t address, uint8_t *value);
bool cpm80CpuWriteMemory(cpm80Cpu *cpu, uint16_t address, uint8_t value);
bool cpm80CpuStep(cpm80Cpu *cpu);
