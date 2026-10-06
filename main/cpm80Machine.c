#include "cpm80Machine.h"
#include "cpm80Guest.h"
#include "cpm80DriveConfig.h"
#include "hostExchange.h"
#include "hostConsole.h"
#include "imageFile.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include <stdlib.h>
#include <string.h>

static const char *tag = "cpm80Machine";
static const char *driveConfigPath = "/microSD/retro/images/cpm80/drives.cfg";
static const uint8_t systemHeader[cpm80SystemHeaderSize] = {'R', 'E', 'T', 'R', 'O', 'C', 'P', 'M', 1, 1, 0x00, 0xC4,
                                                          0x00, 0xCC, 0x00, 0xDA};
static const uint8_t expectedSystemDiskHash[32] = {0x96, 0x0B, 0xFE, 0x75, 0x2F, 0xC8, 0x48, 0x94,
                                                   0x41, 0xC2, 0x46, 0xB6, 0xA8, 0x8D, 0x77, 0x64,
                                                   0xFF, 0xCE, 0xC0, 0xF8, 0xE1, 0x72, 0x55, 0x10,
                                                   0x1A, 0xCB, 0xCC, 0xA8, 0x57, 0x0A, 0xEB, 0xB7};
static imageFile diskImages[cpm80DiskDriveCount];
static cpm80DriveConfig driveTable[cpm80DiskDriveCount];
static cpm80Guest guest;
static hostExchange exchangeService;
static bool guestReady;

static bool consoleAvailable(void *context)
{
  (void)context;
  return hostConsoleCharAvailable();
}

static int consoleRead(void *context)
{
  (void)context;
  return hostConsoleGetChar();
}

static void consoleWrite(void *context, uint8_t character)
{
  (void)context;
  hostConsolePutChar((char)character);
}

static bool diskDriveAvailable(void *context, uint8_t drive)
{
  (void)context;
  return drive < cpm80DiskDriveCount && diskImages[drive].file != NULL;
}

static bool diskDriveProfile(void *context, uint8_t drive, cpm80DiskProfile *profile)
{
  (void)context;
  if (drive >= cpm80DiskDriveCount || profile == NULL || diskImages[drive].file == NULL)
  {
    return false;
  }
  *profile = driveTable[drive].profile;
  return true;
}

static uint16_t diskSectorsPerTrack(uint8_t drive)
{
  return driveTable[drive].profile == cpm80DiskProfileLarge ? cpm80LargeDiskSectorsPerTrack : cpm80DiskSectorsPerTrack;
}

static bool diskReadRecord(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                           uint8_t record[cpm80DiskSectorSize])
{
  (void)context;
  if (drive >= cpm80DiskDriveCount || track >= cpm80DiskTracks || sector >= diskSectorsPerTrack(drive))
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)track * diskSectorsPerTrack(drive) + sector;
  return imageReadAt(&diskImages[drive], recordIndex * cpm80DiskSectorSize, record, cpm80DiskSectorSize);
}

static bool diskWriteRecord(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                            const uint8_t record[cpm80DiskSectorSize])
{
  (void)context;
  if (drive >= cpm80DiskDriveCount || track >= cpm80DiskTracks || sector >= diskSectorsPerTrack(drive))
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)track * diskSectorsPerTrack(drive) + sector;
  return imageWriteAt(&diskImages[drive], recordIndex * cpm80DiskSectorSize, record, cpm80DiskSectorSize) &&
         imageFlush(&diskImages[drive]);
}

static void guestYield(void *context)
{
  (void)context;
  vTaskDelay(1);
}

static uint8_t exchangePortInput(void *context, uint8_t port)
{
  return hostExchangePortInput(context, port);
}

static void exchangePortOutput(void *context, uint8_t port, uint8_t value)
{
  hostExchangePortOutput(context, port, value);
}

static void loadDriveConfig(void)
{
  char error[96];
  cpm80DriveConfigResult result = cpm80DriveConfigLoad(driveConfigPath, driveTable, error, sizeof(error));
  if (result == cpm80DriveConfigMissing)
  {
    ESP_LOGW(tag, "%s", error);
  }
  else if (result == cpm80DriveConfigInvalid)
  {
    ESP_LOGE(tag, "%s; using the built-in A: system image only", error);
  }
}

static void openOptionalDiskImages(void)
{
  for (uint8_t drive = 1; drive < cpm80DiskDriveCount; ++drive)
  {
    if (!driveTable[drive].configured)
    {
      continue;
    }
    uint64_t size;
    if (!resourceSize(driveTable[drive].path, &size))
    {
      ESP_LOGW(tag, "CP/M %c: image is missing: %s", 'A' + drive, driveTable[drive].path);
      continue;
    }
    uint64_t expectedSize = driveTable[drive].profile == cpm80DiskProfileLarge ? cpm80LargeImageSize : cpm80SystemImageSize;
    if (size != expectedSize)
    {
      ESP_LOGE(tag, "Ignoring CP/M %c: image %s: size=%llu, expected %s profile size=%llu", 'A' + drive,
               driveTable[drive].path, (unsigned long long)size,
               driveTable[drive].profile == cpm80DiskProfileLarge ? "LARGE" : "SYSTEM",
               (unsigned long long)expectedSize);
      continue;
    }
    if (!imageOpen(&diskImages[drive], driveTable[drive].path, driveTable[drive].readOnly))
    {
      ESP_LOGE(tag, "Could not open CP/M %c: image: %s", 'A' + drive, driveTable[drive].path);
    }
  }
}

static void closeDiskImages(void)
{
  for (uint8_t drive = 0; drive < cpm80DiskDriveCount; ++drive)
  {
    if (diskImages[drive].file != NULL && !imageClose(&diskImages[drive]))
    {
      ESP_LOGE(tag, "Failed to close CP/M %c: image: %s", 'A' + drive, driveTable[drive].path);
    }
  }
}

static bool readSystemImageHeader(imageFile *image)
{
  uint8_t header[cpm80SystemHeaderSize];
  return imageSize(image) == cpm80SystemImageSize &&
         imageReadAt(image, cpm80SystemHeaderOffset, header, sizeof(header)) &&
         memcmp(header, systemHeader, sizeof(header)) == 0;
}

static bool calculateSystemImageHash(imageFile *image, uint8_t hash[32])
{
  const mbedtls_md_info_t *hashInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (hashInfo == NULL)
  {
    return false;
  }

  mbedtls_md_context_t hashContext;
  mbedtls_md_init(&hashContext);
  int result = mbedtls_md_setup(&hashContext, hashInfo, 0);
  if (result == 0)
  {
    result = mbedtls_md_starts(&hashContext);
  }

  uint8_t buffer[512];
  for (uint64_t offset = 0; result == 0 && offset < imageSize(image); offset += sizeof(buffer))
  {
    size_t remaining = (size_t)(imageSize(image) - offset);
    size_t length = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (!imageReadAt(image, offset, buffer, length))
    {
      result = -1;
    }
    else
    {
      result = mbedtls_md_update(&hashContext, buffer, length);
    }
  }
  if (result == 0)
  {
    result = mbedtls_md_finish(&hashContext, hash);
  }
  mbedtls_md_free(&hashContext);
  return result == 0;
}

static void formatHash(const uint8_t hash[32], char text[65])
{
  static const char digits[] = "0123456789abcdef";
  for (size_t index = 0; index < 32; ++index)
  {
    text[index * 2] = digits[hash[index] >> 4];
    text[index * 2 + 1] = digits[hash[index] & 0x0F];
  }
  text[64] = '\0';
}

machineState cpm80MachineProbe(const retroMachine *machine)
{
  if (machine == NULL || !machine->implemented)
  {
    return machineNotImplemented;
  }

  loadDriveConfig();
  const char *systemDiskPath = driveTable[0].path;
  imageFile image = {0};
  if (!imageOpen(&image, systemDiskPath, true))
  {
    uint64_t size;
    if (resourceSize(systemDiskPath, &size))
    {
      ESP_LOGE(tag, "Could not open CP/M system image %s (size=%llu)", systemDiskPath, (unsigned long long)size);
      return machineResourceInvalid;
    }
    return machineMissingResource;
  }

  bool valid = readSystemImageHeader(&image);
  if (!valid)
  {
    if (imageSize(&image) != cpm80SystemImageSize)
    {
      ESP_LOGE(tag, "Invalid CP/M system image %s: size=%llu, expected=%u", systemDiskPath,
               (unsigned long long)imageSize(&image), (unsigned)cpm80SystemImageSize);
    }
    else
    {
      ESP_LOGE(tag, "Invalid CP/M system image header signature: %s", systemDiskPath);
    }
  }
  if (valid)
  {
    uint8_t actualHash[32];
    if (!calculateSystemImageHash(&image, actualHash))
    {
      ESP_LOGE(tag, "Could not calculate CP/M system image SHA-256: %s", systemDiskPath);
      valid = false;
    }
    else if (memcmp(actualHash, expectedSystemDiskHash, sizeof(actualHash)) != 0)
    {
      char actualHashText[65];
      char expectedHashText[65];
      formatHash(actualHash, actualHashText);
      formatHash(expectedSystemDiskHash, expectedHashText);
      ESP_LOGE(tag, "CP/M system image checksum mismatch for %s: actual=%s expected=%s", systemDiskPath,
               actualHashText, expectedHashText);
      valid = false;
    }
  }
  if (!imageClose(&image))
  {
    ESP_LOGE(tag, "Failed to close CP/M system image during probe: %s", systemDiskPath);
    return machineResourceInvalid;
  }
  return valid ? machineAvailable : machineResourceInvalid;
}

esp_err_t cpm80MachineInitialize(void)
{
  if (guestReady)
  {
    ESP_LOGE(tag, "CP/M guest is already initialized");
    return ESP_FAIL;
  }
  loadDriveConfig();
  if (!imageOpen(&diskImages[0], driveTable[0].path, true))
  {
    ESP_LOGE(tag, "Could not open CP/M system image: %s", driveTable[0].path);
    return ESP_FAIL;
  }

  if (!readSystemImageHeader(&diskImages[0]))
  {
    if (imageSize(&diskImages[0]) != cpm80SystemImageSize)
    {
      ESP_LOGE(tag, "Invalid CP/M system image %s: size=%llu, expected=%u", driveTable[0].path,
               (unsigned long long)imageSize(&diskImages[0]), (unsigned)cpm80SystemImageSize);
    }
    else
    {
      ESP_LOGE(tag, "Invalid CP/M system image header signature: %s", driveTable[0].path);
    }
    if (!imageClose(&diskImages[0]))
    {
      ESP_LOGE(tag, "Failed to close invalid CP/M system image");
    }
    return ESP_ERR_INVALID_SIZE;
  }

  size_t systemBinarySize = cpm80CcpSize + cpm80BdosSize;
  uint8_t *systemBinaries = malloc(systemBinarySize);
  if (systemBinaries == NULL)
  {
    ESP_LOGE(tag, "Could not allocate %u bytes for CP/M system binaries", (unsigned)systemBinarySize);
    if (!imageClose(&diskImages[0]))
    {
      ESP_LOGE(tag, "Failed to close CP/M system image after allocation failure");
    }
    return ESP_ERR_NO_MEM;
  }
  if (!imageReadAt(&diskImages[0], 0, systemBinaries, cpm80CcpSize) ||
      !imageReadAt(&diskImages[0], cpm80CcpSize, systemBinaries + cpm80CcpSize, cpm80BdosSize))
  {
    ESP_LOGE(tag, "Failed reading CP/M system binaries from %s", driveTable[0].path);
    free(systemBinaries);
    if (!imageClose(&diskImages[0]))
    {
      ESP_LOGE(tag, "Failed to close CP/M system image after a read error");
    }
    return ESP_FAIL;
  }

  openOptionalDiskImages();

  if (!hostExchangeInitialize(&exchangeService, "/microSD/retro/exchange/cpm80"))
  {
    ESP_LOGE(tag, "Could not initialize the CP/M exchange service");
    closeDiskImages();
    return ESP_FAIL;
  }

  const cpm80HostOps host = {.consoleAvailable = consoleAvailable,
                           .consoleRead = consoleRead,
                           .consoleWrite = consoleWrite,
                           .diskDriveAvailable = diskDriveAvailable,
                           .diskDriveProfile = diskDriveProfile,
                           .diskReadRecord = diskReadRecord,
                           .diskWriteRecord = diskWriteRecord,
                           .exchangePortInput = exchangePortInput,
                           .exchangePortOutput = exchangePortOutput,
                           .yield = guestYield,
                           .context = &exchangeService};
  bool guestInitialized =
      cpm80GuestInitialize(&guest, &host, systemBinaries, systemBinaries + cpm80CcpSize);
  free(systemBinaries);
  if (!guestInitialized)
  {
    ESP_LOGE(tag, "Could not allocate or initialize CP/M guest memory");
    hostExchangeClose(&exchangeService);
    closeDiskImages();
    return ESP_ERR_NO_MEM;
  }
  guestReady = true;
  return ESP_OK;
}

void cpm80MachineRun(void)
{
  if (!guestReady || !cpm80GuestColdBoot(&guest))
  {
    puts("CP/M guest is not initialized.");
    return;
  }

  puts("\nStarting CP/M-80 on the virtual A: disk.");
  while (!guest.cpu.processor.halted)
  {
    if (cpm80GuestRunFor(&guest, 10000) == 0)
    {
      break;
    }
    vTaskDelay(1);
  }

  puts("\nCP/M stopped.");
  cpm80GuestDestroy(&guest);
  guestReady = false;
  hostExchangeClose(&exchangeService);
  closeDiskImages();
}
