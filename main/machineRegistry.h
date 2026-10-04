#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

typedef enum
{
  machineAvailable,
  machineNotImplemented,
  machineMissingResource,
  machineSdRequired,
  machineResourceInvalid
} machineState;

typedef struct
{
  const char *path;
  size_t minimumSize;
} machineResource;

typedef struct retroMachine retroMachine;
struct retroMachine
{
  const char *name;
  const char *id;
  const char *design;
  bool implemented;
  bool requiresSd;
  const machineResource *requiredResources;
  size_t resourceCount;
  machineState (*probe)(const retroMachine *machine);
  esp_err_t (*init)(void);
  void (*run)(void);
};

size_t machineCount(void);
const retroMachine *machineGet(size_t index);
const char *machineStateText(machineState state);
void machineInspectResources(void);
