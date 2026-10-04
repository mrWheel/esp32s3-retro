#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "z80.h"

enum
{
  cpmMemorySize = 65536
};

typedef uint8_t (*cpmPortInput)(void *context, uint8_t port);
typedef void (*cpmPortOutput)(void *context, uint8_t port, uint8_t value);

typedef struct
{
  z80 processor;
  uint8_t *memory;
  cpmPortInput portInput;
  cpmPortOutput portOutput;
  void *portContext;
  bool initialized;
} cpmCpu;

//— Initialize instances with {0}; destroy them before initializing them again.
bool cpmCpuInitialize(cpmCpu *cpu, cpmPortInput portInput, cpmPortOutput portOutput, void *portContext);
void cpmCpuDestroy(cpmCpu *cpu);
bool cpmCpuLoad(cpmCpu *cpu, uint16_t address, const uint8_t *data, size_t length);
bool cpmCpuReadMemory(const cpmCpu *cpu, uint16_t address, uint8_t *value);
bool cpmCpuWriteMemory(cpmCpu *cpu, uint16_t address, uint8_t value);
bool cpmCpuStep(cpmCpu *cpu);
