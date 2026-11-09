/*
 * Copyright (C) 2026 Willem Aandewiel
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "machineRegistry.h"
#include "cpm80Guest.h"
#include "cpm80Machine.h"
#include "cpm86Machine.h"
#include "apple2Machine.h"
#include "storage.h"
#include <stdio.h>
#include <sys/stat.h>

static const machineResource cpm80Resources[] = {
    {"/littlefs/cpm80/system.dsk", cpm80SystemImageSize}};
static const machineResource cpm86Resources[] = {
    {"/littlefs/cpm86/cpm.sys", cpm86SystemFileSize},
    {"/littlefs/cpm86/retro86bios.h86", 1},
    {"/littlefs/cpm86/system.dsk", cpm86SystemDiskSize}};
static const char* apple2ConfigPath = "/littlefs/apple2/drives.cfg";
static const char* ucsdConfigPath = "/littlefs/ucsd/drives.cfg";

static machineState probeMachine(const retroMachine* machine)
{
  if (!machine->implemented)
  {
    return machineNotImplemented;
  }
  if (machine->requiresSd && !storageReady())
  {
    return machineSdRequired;
  }
  for (size_t index = 0; index < machine->resourceCount; ++index)
  {
    struct stat info;
    const machineResource* resource = &machine->requiredResources[index];
    if (stat(resource->path, &info) != 0)
    {
      return machineMissingResource;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0 || (size_t)info.st_size < resource->minimumSize)
    {
      return machineResourceInvalid;
    }
  }
  return machineAvailable;
}

static esp_err_t initializePlaceholder(void)
{
  return ESP_ERR_NOT_SUPPORTED;
}

static void runPlaceholder(void)
{
  puts("Not implemented yet.");
}

static machineState probeApple2(const retroMachine* machine)
{
  return apple2MachineProbeWithConfig(machine, apple2ConfigPath);
}

static esp_err_t initializeApple2(void)
{
  return apple2MachineInitializeWithConfig(apple2ConfigPath);
}

static machineState probeUcsd(const retroMachine* machine)
{
  return apple2MachineProbeWithConfig(machine, ucsdConfigPath);
}

static esp_err_t initializeUcsd(void)
{
  return apple2MachineInitializeWithConfig(ucsdConfigPath);
}

static const retroMachine machines[] = {
    {"CP/M-80", "cpm80", "designCPM80.md", true, false, cpm80Resources, 1, cpm80MachineProbe,
     cpm80MachineInitialize, cpm80MachineRun},
    {"CP/M-86", "cpm86", "designCPM86.md", true, false, cpm86Resources, 3, cpm86MachineProbe,
     cpm86MachineInitialize, cpm86MachineRun},
    {"UCSD Pascal", "ucsd", "designUCSD.md", true, false, NULL, 0, probeUcsd, initializeUcsd,
     apple2MachineRun},
    {"Apple II", "apple2", "designApple2.md", true, false, NULL, 0, probeApple2, initializeApple2,
     apple2MachineRun},
    {"SWTPC 6800", "swtpc", "designSWTPC.md", false, false, NULL, 0, probeMachine,
     initializePlaceholder, runPlaceholder}};

size_t machineCount(void)
{
  return sizeof(machines) / sizeof(machines[0]);
}

const retroMachine* machineGet(size_t index)
{
  return index < machineCount() ? &machines[index] : NULL;
}

const char* machineStateText(machineState state)
{
  static const char* names[] = {"available",        "Writable!",   "not implemented",
                                "missing resource", "SD required", "invalid resource"};
  return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}

void machineInspectResources(void)
{
  for (size_t index = 0; index < machineCount(); ++index)
  {
    char path[64];
    snprintf(path, sizeof(path), "/littlefs/%s", machines[index].id);
    printf("%s: %s; design: %s\n", path,
           storageDirectoryExists(path) ? "directory present" : "directory missing",
           machines[index].design);
  }
}
