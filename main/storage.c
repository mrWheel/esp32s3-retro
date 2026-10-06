#include "storage.h"
#include "hostCore.h"
#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "esp_littlefs.h"
#include "ff.h"
#include "diskio_sdmmc.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static sdmmc_card_t *sdCard;
static bool busReady;
static bool layoutReady;
static char statusDetail[160];
static const char *statusText = "SD not checked";
static const char *machineIds[] = {"cpm80", "cpm86", "ucsd", "apple2", "swtpc"};

bool storageDirectoryExists(const char *path)
{
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

bool storageBootMount(void)
{
  esp_vfs_littlefs_conf_t config = {
      .base_path = "/littlefs", .partition_label = "bootfs", .format_if_mount_failed = false, .dont_mount = false};
  esp_err_t result = esp_vfs_littlefs_register(&config);
  if (result != ESP_OK)
  {
    printf("LittleFS unavailable: %s. Flash the bootfs image; no auto-format.\n", esp_err_to_name(result));
    return false;
  }
  return true;
}

static bool mountCard(void)
{
  if (!busReady)
  {
    spi_bus_config_t busConfig = {.mosi_io_num = CONFIG_RETRO_SD_MOSI,
                                  .miso_io_num = CONFIG_RETRO_SD_MISO,
                                  .sclk_io_num = CONFIG_RETRO_SD_CLK,
                                  .quadwp_io_num = -1,
                                  .quadhd_io_num = -1,
                                  .max_transfer_sz = 4096};
    esp_err_t result = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
    if (result != ESP_OK)
    {
      statusText = "SD SPI initialization failed: check GPIO configuration";
      return false;
    }
    busReady = true;
  }
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.max_freq_khz = 10000;
  sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
  device.gpio_cs = CONFIG_RETRO_SD_CS;
  device.host_id = SPI2_HOST;
  esp_vfs_fat_mount_config_t mountConfig = {
      .format_if_mount_failed = false, .max_files = 6, .allocation_unit_size = 16384};
  esp_err_t result = esp_vfs_fat_sdspi_mount("/microSD", &host, &device, &mountConfig, &sdCard);
  if (result != ESP_OK)
  {
    sdCard = NULL;
    statusText = "SD missing, unreadable or unsupported filesystem; never auto-formatted";
    return false;
  }
  return true;
}

bool storageRefresh(void)
{
  layoutReady = false;
  if (sdCard != NULL)
  {
    esp_err_t result = esp_vfs_fat_sdcard_unmount("/microSD", sdCard);
    if (result != ESP_OK)
    {
      statusText = "SD unmount failed; reset before retrying";
      return false;
    }
    sdCard = NULL;
  }
  if (!mountCard())
  {
    return false;
  }
  char drive[16];
  snprintf(drive, sizeof(drive), "%u:", (unsigned)ff_diskio_get_pdrv_card(sdCard));
  FATFS *fileSystem = NULL;
  DWORD freeClusters = 0;
  if (f_getfree(drive, &freeClusters, &fileSystem) != FR_OK || fileSystem == NULL || fileSystem->fs_type != FS_FAT32)
  {
    statusText = "SD must use FAT32 (FAT12/FAT16/exFAT are not accepted)";
    return false;
  }
  FILE *file = fopen("/microSD/retro/layout.txt", "rb");
  if (file == NULL)
  {
    statusText = "Missing /retro/layout.txt; copy the supplied SD layout";
    return false;
  }
  char marker[64];
  size_t length = fread(marker, 1, sizeof(marker), file);
  bool readOk = !ferror(file);
  fclose(file);
  if (!readOk || !layoutIsValid(marker, length))
  {
    statusText = "Invalid /retro/layout.txt; expected ESP32-S3-RETRO then layout=1";
    return false;
  }
  const char *groups[] = {"images", "exchange"};
  for (size_t group = 0; group < 2; ++group)
  {
    for (size_t machine = 0; machine < 5; ++machine)
    {
      char path[96];
      snprintf(path, sizeof(path), "/microSD/retro/%s/%s", groups[group], machineIds[machine]);
      if (!storageDirectoryExists(path))
      {
        snprintf(statusDetail, sizeof(statusDetail), "Missing required directory: %s", path);
        statusText = statusDetail;
        return false;
      }
    }
  }
  if (!storageDirectoryExists("/microSD/retro/exchange/common") || !storageDirectoryExists("/microSD/retro/backup"))
  {
    statusText = "SD layout incomplete: exchange/common or backup is missing";
    return false;
  }
  statusText = "FAT32 SD present; retro layout v1 valid";
  layoutReady = true;
  return true;
}

bool storageReady(void)
{
  return layoutReady;
}

const char *storageStatus(void)
{
  return statusText;
}
