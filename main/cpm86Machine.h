#pragma once

#include "cpm86DriveConfig.h"
#include "machineRegistry.h"

enum
{
  cpm86SystemFileSize = 10240
};

machineState cpm86MachineProbe(const retroMachine *machine);
esp_err_t cpm86MachineInitialize(void);
void cpm86MachineRun(void);
