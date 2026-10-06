#pragma once
#include "machineRegistry.h"

machineState cpm80MachineProbe(const retroMachine *machine);
esp_err_t cpm80MachineInitialize(void);
void cpm80MachineRun(void);
