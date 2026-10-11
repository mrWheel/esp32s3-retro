#pragma once

#include "machineRegistry.h"

machineState apple2MachineProbeWithConfig(const retroMachine *machine, const char *configPath);
esp_err_t apple2MachineInitializeWithConfig(const char *configPath);
void apple2MachineRun(void);
