#include "hostConsole.h"
#include "systemMenu.h"
#include "storage.h"
#include "machineRegistry.h"
#include "diskActivity.h"
#include "esp_log.h"
#include <stdio.h>

static const char *tag = "main";

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
