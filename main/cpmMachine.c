#include "cpmMachine.h"
#include "cpmGuest.h"
#include "hostConsole.h"
#include "imageFile.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include <stdlib.h>
#include <string.h>

static const char *tag = "cpmMachine";
static const char *systemDiskPath = "/littlefs/cpm/system.dsk";
static const uint8_t systemHeader[cpmSystemHeaderSize] = {'R', 'E', 'T', 'R', 'O', 'C', 'P', 'M', 1, 1, 0x00, 0xC4,
                                                          0x00, 0xCC, 0x00, 0xDA};
static const uint8_t expectedSystemDiskHash[32] = {0xA6, 0x51, 0x47, 0x0E, 0x4C, 0xF5, 0xB2, 0xC4,
                                                   0x0B, 0xA1, 0xC7, 0x36, 0xE8, 0xD5, 0x34, 0xEE,
                                                   0xAA, 0x96, 0x79, 0x63, 0x1D, 0xD2, 0xF3, 0x37,
                                                   0x65, 0xD1, 0x53, 0xD7, 0xB3, 0xE6, 0xBD, 0x8D};
static imageFile systemDisk;
static cpmGuest guest;
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

static bool diskReadRecord(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                           uint8_t record[cpmDiskSectorSize])
{
  (void)context;
  if (drive != 0 || track >= cpmDiskTracks || sector >= cpmDiskSectorsPerTrack)
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)track * cpmDiskSectorsPerTrack + sector;
  return imageReadAt(&systemDisk, recordIndex * cpmDiskSectorSize, record, cpmDiskSectorSize);
}

static void guestYield(void *context)
{
  (void)context;
  vTaskDelay(1);
}

static bool readSystemImageHeader(imageFile *image)
{
  uint8_t header[cpmSystemHeaderSize];
  return imageSize(image) == cpmSystemImageSize &&
         imageReadAt(image, cpmSystemHeaderOffset, header, sizeof(header)) &&
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

machineState cpmMachineProbe(const retroMachine *machine)
{
  if (machine == NULL || !machine->implemented)
  {
    return machineNotImplemented;
  }

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
    if (imageSize(&image) != cpmSystemImageSize)
    {
      ESP_LOGE(tag, "Invalid CP/M system image %s: size=%llu, expected=%u", systemDiskPath,
               (unsigned long long)imageSize(&image), (unsigned)cpmSystemImageSize);
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

esp_err_t cpmMachineInitialize(void)
{
  if (guestReady)
  {
    ESP_LOGE(tag, "CP/M guest is already initialized");
    return ESP_FAIL;
  }
  if (!imageOpen(&systemDisk, systemDiskPath, true))
  {
    ESP_LOGE(tag, "Could not open CP/M system image: %s", systemDiskPath);
    return ESP_FAIL;
  }

  if (!readSystemImageHeader(&systemDisk))
  {
    if (imageSize(&systemDisk) != cpmSystemImageSize)
    {
      ESP_LOGE(tag, "Invalid CP/M system image %s: size=%llu, expected=%u", systemDiskPath,
               (unsigned long long)imageSize(&systemDisk), (unsigned)cpmSystemImageSize);
    }
    else
    {
      ESP_LOGE(tag, "Invalid CP/M system image header signature: %s", systemDiskPath);
    }
    if (!imageClose(&systemDisk))
    {
      ESP_LOGE(tag, "Failed to close invalid CP/M system image");
    }
    return ESP_ERR_INVALID_SIZE;
  }

  size_t systemBinarySize = cpmCcpSize + cpmBdosSize;
  uint8_t *systemBinaries = malloc(systemBinarySize);
  if (systemBinaries == NULL)
  {
    ESP_LOGE(tag, "Could not allocate %u bytes for CP/M system binaries", (unsigned)systemBinarySize);
    if (!imageClose(&systemDisk))
    {
      ESP_LOGE(tag, "Failed to close CP/M system image after allocation failure");
    }
    return ESP_ERR_NO_MEM;
  }
  if (!imageReadAt(&systemDisk, 0, systemBinaries, cpmCcpSize) ||
      !imageReadAt(&systemDisk, cpmCcpSize, systemBinaries + cpmCcpSize, cpmBdosSize))
  {
    ESP_LOGE(tag, "Failed reading CP/M system binaries from %s", systemDiskPath);
    free(systemBinaries);
    if (!imageClose(&systemDisk))
    {
      ESP_LOGE(tag, "Failed to close CP/M system image after a read error");
    }
    return ESP_FAIL;
  }

  const cpmHostOps host = {.consoleAvailable = consoleAvailable,
                           .consoleRead = consoleRead,
                           .consoleWrite = consoleWrite,
                           .diskReadRecord = diskReadRecord,
                           .yield = guestYield,
                           .context = NULL};
  bool guestInitialized =
      cpmGuestInitialize(&guest, &host, systemBinaries, systemBinaries + cpmCcpSize);
  free(systemBinaries);
  if (!guestInitialized)
  {
    ESP_LOGE(tag, "Could not allocate or initialize CP/M guest memory");
    if (!imageClose(&systemDisk))
    {
      ESP_LOGE(tag, "Failed to close CP/M system image after initialization failure");
    }
    return ESP_ERR_NO_MEM;
  }
  guestReady = true;
  return ESP_OK;
}

void cpmMachineRun(void)
{
  if (!guestReady || !cpmGuestColdBoot(&guest))
  {
    puts("CP/M guest is not initialized.");
    return;
  }

  puts("\nStarting CP/M 2.2 on the virtual A: disk.");
  while (!guest.cpu.processor.halted)
  {
    if (cpmGuestRunFor(&guest, 10000) == 0)
    {
      break;
    }
    vTaskDelay(1);
  }

  puts("\nCP/M stopped.");
  cpmGuestDestroy(&guest);
  guestReady = false;
  if (!imageClose(&systemDisk))
  {
    puts("Failed to close the CP/M disk image cleanly.");
  }
}
