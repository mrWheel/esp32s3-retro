#pragma once

#include "machineRegistry.h"

machineState apple2MachineProbe(const retroMachine *machine);
esp_err_t apple2MachineInitialize(void);
void apple2MachineRun(void);
