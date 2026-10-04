#include "hostConsole.h"
#include "systemMenu.h"
#include "storage.h"
#include "machineRegistry.h"
#include <stdio.h>

void app_main(void)
{
  esp_err_t result = hostConsoleInit();
  if (result != ESP_OK)
  {
    printf("USB Serial/JTAG initialization failed: %s\n", esp_err_to_name(result));
    return;
  }
  storageBootMount();
  machineInspectResources();
  storageRefresh();
  systemMenuRun();
}
