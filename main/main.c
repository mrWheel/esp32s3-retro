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

#include "hostConsole.h"
#include "systemMenu.h"
#include "storage.h"
#include "machineRegistry.h"
#include "diskActivity.h"
#include "esp_log.h"
#include <stdio.h>

//-- never remove or change this line,
//-- it indicates the program version
const char* PROG_VERSION = "v1.0.0";

static const char* tag = "main";

void app_main(void)
{
  esp_err_t result = hostConsoleInit();
  if (result != ESP_OK)
  {
    printf("USB Serial/JTAG initialization failed: %s\n", esp_err_to_name(result));
    return;
  }
  result = diskActivityInit();
  if (result != ESP_OK)
  {
    ESP_LOGE(tag, "Disk activity RGB LED is unavailable: %s", esp_err_to_name(result));
  }
  storageBootMount();
  machineInspectResources();
  storageRefresh();
  systemMenuRun();
}
