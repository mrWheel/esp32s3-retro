#pragma once

#include "machineRegistry.h"

enum
{
  cpm86SystemFileSize = 10240,
  cpm86SystemDiskSize = 163840
};

machineState cpm86MachineProbe(const retroMachine *machine);
esp_err_t cpm86MachineInitialize(void);
void cpm86MachineRun(void);
