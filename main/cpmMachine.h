#pragma once
#include "machineRegistry.h"

machineState cpmMachineProbe(const retroMachine *machine);
esp_err_t cpmMachineInitialize(void);
void cpmMachineRun(void);
