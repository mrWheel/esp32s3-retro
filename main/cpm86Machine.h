#pragma once

#include "machineRegistry.h"

enum
{
  cpm86SystemFileSize = 10240,
  cpm86SystemDiskSize = 163840,
  cpm86SystemDiskTracks = 40,
  cpm86LargeDiskTracks = 129,
  cpm86LargeDiskSize = 528384
};

machineState cpm86MachineProbe(const retroMachine *machine);
esp_err_t cpm86MachineInitialize(void);
void cpm86MachineRun(void);
