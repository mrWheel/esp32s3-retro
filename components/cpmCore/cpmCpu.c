#include "cpmCpu.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(cpmMemorySize == UINT16_MAX + 1U, "CP/M memory must cover the 16-bit address space");

static uint8_t readByte(void *context, uint16_t address)
{
  cpmCpu *cpu = context;
  return cpu->memory[address];
}

static void writeByte(void *context, uint16_t address, uint8_t value)
{
  cpmCpu *cpu = context;
  cpu->memory[address] = value;
}

static uint8_t inputPort(z80 *processor, uint8_t port)
{
  cpmCpu *cpu = processor->userdata;
  return cpu->portInput(cpu->portContext, port);
}

static void outputPort(z80 *processor, uint8_t port, uint8_t value)
{
  cpmCpu *cpu = processor->userdata;
  cpu->portOutput(cpu->portContext, port, value);
}

bool cpmCpuInitialize(cpmCpu *cpu, cpmPortInput portInputCallback, cpmPortOutput portOutputCallback, void *portContext)
{
  if (cpu == NULL || portInputCallback == NULL || portOutputCallback == NULL)
  {
    return false;
  }
  if (cpu->initialized || cpu->memory != NULL)
  {
    return false;
  }

  cpu->memory = calloc(cpmMemorySize, sizeof(*cpu->memory));
  if (cpu->memory == NULL)
  {
    return false;
  }
  cpu->portInput = portInputCallback;
  cpu->portOutput = portOutputCallback;
  cpu->portContext = portContext;
  z80_init(&cpu->processor);
  cpu->processor.read_byte = readByte;
  cpu->processor.write_byte = writeByte;
  cpu->processor.port_in = inputPort;
  cpu->processor.port_out = outputPort;
  cpu->processor.userdata = cpu;
  cpu->initialized = true;
  return true;
}

void cpmCpuDestroy(cpmCpu *cpu)
{
  if (cpu == NULL)
  {
    return;
  }
  free(cpu->memory);
  memset(cpu, 0, sizeof(*cpu));
}

bool cpmCpuLoad(cpmCpu *cpu, uint16_t address, const uint8_t *data, size_t length)
{
  if (cpu == NULL || !cpu->initialized || (data == NULL && length != 0) || length > cpmMemorySize - address)
  {
    return false;
  }
  if (length > 0)
  {
    memcpy(&cpu->memory[address], data, length);
  }
  return true;
}

bool cpmCpuReadMemory(const cpmCpu *cpu, uint16_t address, uint8_t *value)
{
  if (cpu == NULL || !cpu->initialized || value == NULL)
  {
    return false;
  }
  *value = cpu->memory[address];
  return true;
}

bool cpmCpuWriteMemory(cpmCpu *cpu, uint16_t address, uint8_t value)
{
  if (cpu == NULL || !cpu->initialized)
  {
    return false;
  }
  cpu->memory[address] = value;
  return true;
}

bool cpmCpuStep(cpmCpu *cpu)
{
  if (cpu == NULL || !cpu->initialized)
  {
    return false;
  }
  z80_step(&cpu->processor);
  return true;
}
