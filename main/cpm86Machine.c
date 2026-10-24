#include "cpm86Machine.h"
#include "cpm86BiosOverlay.h"
#include "cpm86Core.h"
#include "cpm86DriveConfig.h"
#include "hostConsole.h"
#include "imageFile.h"
#include "diskActivity.h"
#include "storage.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char* tag = "cpm86Machine";
static const char* systemFilePath = "/littlefs/cpm86/cpm.sys";
static const char* systemDiskPath = "/littlefs/cpm86/system.dsk";
static const char* biosOverlayPath = "/littlefs/cpm86/retro86bios.h86";
static const char* driveConfigPath = "/littlefs/cpm86/drives.cfg";

typedef enum
{
  diskTransferIdle,
  diskTransferRead,
  diskTransferWrite
} diskTransferMode;

typedef struct
{
  imageFile image;
  bool configured;
  uint32_t tracks;
  uint8_t geometryCode;
} cpm86DiskDrive;

typedef struct
{
  cpm86DiskDrive drives[cpm86DiskDriveCount];
  uint8_t record[128];
  uint16_t track;
  uint16_t sector;
  uint8_t drive;
  size_t transferIndex;
  uint8_t transferStatus;
  diskTransferMode transferMode;
} cpm86Disk;

static cpm86Core* guestCore;
static cpm86Disk diskController;
static bool guestReady;
static bool guestStopRequested;

static uint32_t readMilliseconds(void* context)
{
  (void)context;
  return (uint32_t)((uint64_t)esp_timer_get_time() / 1000ULL);
}

static bool diskRecordOffset(uint64_t* offset)
{
  if (offset == NULL || diskController.drive >= cpm86DiskDriveCount ||
      !diskController.drives[diskController.drive].configured || diskController.track == 0 ||
      diskController.track >= diskController.drives[diskController.drive].tracks ||
      diskController.sector >= 32)
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)diskController.track * 32 + diskController.sector;
  *offset = recordIndex * sizeof(diskController.record);
  imageFile* image = &diskController.drives[diskController.drive].image;
  return *offset <= imageSize(image) && sizeof(diskController.record) <= imageSize(image) - *offset;
}

static bool portRead(void* context, uint16_t port, uint8_t* value)
{
  (void)context;
  if (value == NULL)
  {
    return false;
  }

  if (port == 0x00E0)
  {
    *value = hostConsoleCharAvailable() ? 0xFF : 0;
    return true;
  }
  if (port == 0x00E1)
  {
    int character = hostConsoleGetChar();
    if (character < 0)
    {
      return false;
    }
    if (character == 0x1D)
    {
      guestStopRequested = true;
      return false;
    }
    *value = (uint8_t)character;
    return true;
  }
  if (port == 0x00ED)
  {
    *value = diskController.transferStatus;
    return true;
  }
  if (port == 0x00EE && diskController.transferMode == diskTransferRead &&
      diskController.transferIndex < sizeof(diskController.record))
  {
    *value = diskController.record[diskController.transferIndex++];
    return true;
  }
  if (port == 0x00EF)
  {
    bool available = diskController.drive < cpm86DiskDriveCount &&
                     diskController.drives[diskController.drive].configured;
    *value = !available ? 0 : diskController.drives[diskController.drive].geometryCode;
    return true;
  }
  return false;
}

static bool startDiskTransfer(uint8_t command)
{
  uint64_t offset;
  diskController.transferIndex = 0;
  diskController.transferStatus = 1;
  diskController.transferMode = diskTransferIdle;
  if (!diskRecordOffset(&offset))
  {
    return true;
  }

  imageFile* image = &diskController.drives[diskController.drive].image;
  if (command == 0)
  {
    diskActivityRead();
    if (!imageReadAt(image, offset, diskController.record, sizeof(diskController.record)))
    {
      return true;
    }
    diskController.transferStatus = 0;
    diskController.transferMode = diskTransferRead;
  }
  else if (command == 1)
  {
    diskController.transferStatus = 0;
    diskController.transferMode = diskTransferWrite;
  }
  return true;
}

static bool portWrite(void* context, uint16_t port, uint8_t value)
{
  (void)context;
  if (port == 0x00E2)
  {
    hostConsolePutChar((char)value);
    return true;
  }
  if (port == 0x00E8)
  {
    diskController.drive = value;
    return true;
  }
  if (port == 0x00E9)
  {
    diskController.track = (diskController.track & 0xFF00) | value;
    return true;
  }
  if (port == 0x00EA)
  {
    diskController.track = (diskController.track & 0x00FF) | ((uint16_t)value << 8);
    return true;
  }
  if (port == 0x00EB)
  {
    diskController.sector = (diskController.sector & 0xFF00) | value;
    return true;
  }
  if (port == 0x00EC)
  {
    diskController.sector = (diskController.sector & 0x00FF) | ((uint16_t)value << 8);
    return true;
  }
  if (port == 0x00ED)
  {
    return startDiskTransfer(value);
  }
  if (port == 0x00EE && diskController.transferMode == diskTransferWrite &&
      diskController.transferIndex < sizeof(diskController.record))
  {
    diskController.record[diskController.transferIndex++] = value;
    if (diskController.transferIndex == sizeof(diskController.record))
    {
      uint64_t offset;
      imageFile* image = &diskController.drives[diskController.drive].image;
      bool writeSucceeded = diskRecordOffset(&offset);
      if (writeSucceeded)
      {
        diskActivityWrite();
        writeSucceeded =
            imageWriteAt(image, offset, diskController.record, sizeof(diskController.record)) &&
            imageFlush(image);
      }
      if (!writeSucceeded)
      {
        diskController.transferStatus = 1;
      }
      diskController.transferMode = diskTransferIdle;
    }
    return true;
  }
  return false;
}

static bool loadSystemFile(void)
{
  imageFile image = {0};
  if (!imageOpen(&image, systemFilePath, true))
  {
    ESP_LOGE(tag, "Could not open CP/M-86 system file: %s", systemFilePath);
    return false;
  }

  uint8_t header[128];
  bool valid = imageSize(&image) == cpm86SystemFileSize &&
               imageReadAt(&image, 0, header, sizeof(header)) && header[0] == 1 &&
               header[3] == 0x51 && header[4] == 0;
  if (!valid)
  {
    ESP_LOGE(tag, "Invalid CP/M-86 system file or load segment: %s", systemFilePath);
    if (!imageClose(&image))
    {
      ESP_LOGE(tag, "Failed to close invalid CP/M-86 system file: %s", systemFilePath);
    }
    return false;
  }

  size_t payloadSize = (size_t)(imageSize(&image) - sizeof(header));
  uint8_t payload[512];
  size_t payloadOffset = 0;
  while (payloadOffset < payloadSize)
  {
    size_t chunkSize = payloadSize - payloadOffset;
    if (chunkSize > sizeof(payload))
    {
      chunkSize = sizeof(payload);
    }
    if (!imageReadAt(&image, sizeof(header) + payloadOffset, payload, chunkSize))
    {
      ESP_LOGE(tag, "Failed to read CP/M-86 system payload: %s", systemFilePath);
      if (!imageClose(&image))
      {
        ESP_LOGE(tag, "Failed to close CP/M-86 system file after read error: %s", systemFilePath);
      }
      return false;
    }
    cpm86CoreResult loadResult =
        cpm86CoreLoad(guestCore, 0x00510 + (uint32_t)payloadOffset, payload, chunkSize);
    if (loadResult != cpm86CoreOk)
    {
      ESP_LOGE(tag, "Could not load CP/M-86 system payload: result=%d", loadResult);
      if (!imageClose(&image))
      {
        ESP_LOGE(tag, "Failed to close CP/M-86 system file after load error: %s", systemFilePath);
      }
      return false;
    }
    payloadOffset += chunkSize;
  }
  if (!imageClose(&image))
  {
    ESP_LOGE(tag, "Failed to close CP/M-86 system file: %s", systemFilePath);
    return false;
  }
  if (!cpm86BiosLoadOverlay(guestCore, biosOverlayPath))
  {
    ESP_LOGE(tag, "Could not load CP/M-86 BIOS overlay: %s", biosOverlayPath);
    return false;
  }

  const uint8_t bdosVector[] = {0x06, 0x0B, 0x51, 0x00};
  if (cpm86CoreWrite(guestCore, 0x00380, bdosVector, sizeof(bdosVector)) != cpm86CoreOk ||
      cpm86CoreSetEntry(guestCore, 0x0051, 0x2500) != cpm86CoreOk)
  {
    ESP_LOGE(tag, "Could not initialize CP/M-86 entry state");
    return false;
  }
  return true;
}

static void closeDiskImages(void)
{
  for (size_t drive = 0; drive < cpm86DiskDriveCount; ++drive)
  {
    if (diskController.drives[drive].image.file != NULL &&
        !imageClose(&diskController.drives[drive].image))
    {
      ESP_LOGE(tag, "Failed to close CP/M-86 drive %c:", (char)('A' + drive));
    }
  }
  memset(&diskController, 0, sizeof(diskController));
}

machineState cpm86MachineProbe(const retroMachine* machine)
{
  if (machine == NULL || !machine->implemented)
  {
    return machineNotImplemented;
  }
  cpm86DriveConfig drives[cpm86DiskDriveCount];
  char configError[128];
  cpm86DriveConfigResult configResult =
      cpm86DriveConfigLoad(driveConfigPath, drives, configError, sizeof(configError));
  uint64_t systemSize;
  if (!resourceSize(systemFilePath, &systemSize))
  {
    return machineMissingResource;
  }
  if (systemSize != cpm86SystemFileSize)
  {
    ESP_LOGE(tag, "Invalid CP/M-86 system file size: %s", systemFilePath);
    return machineResourceInvalid;
  }
  uint64_t biosOverlaySize;
  if (!resourceSize(biosOverlayPath, &biosOverlaySize) || biosOverlaySize == 0 ||
      biosOverlaySize > 4096)
  {
    ESP_LOGE(tag, "Invalid CP/M-86 BIOS overlay: %s", biosOverlayPath);
    return machineResourceInvalid;
  }
  uint64_t diskSize;
  if (!resourceSize(systemDiskPath, &diskSize))
  {
    return machineMissingResource;
  }
  cpm86DiskProfile systemProfile;
  if (!cpm86DriveConfigSystemProfileFromSize(diskSize, &systemProfile))
  {
    ESP_LOGE(tag,
             "Invalid CP/M-86 system disk size %llu (expected %u or %u): %s; A: is not mounted",
             (unsigned long long)diskSize, (unsigned)cpm86SystemDiskSize,
             (unsigned)cpm86LargeDiskSize, systemDiskPath);
    return machineResourceInvalid;
  }
  imageFile systemDisk = {0};
  if (!imageOpen(&systemDisk, drives[0].path, drives[0].readOnly))
  {
    return machineResourceInvalid;
  }
  if (!imageClose(&systemDisk))
  {
    return machineResourceInvalid;
  }
  return configResult == cpm86DriveConfigLoaded && !drives[0].readOnly ? machineWritable
                                                                       : machineAvailable;
}

esp_err_t cpm86MachineInitialize(void)
{
  if (guestReady || guestCore != NULL)
  {
    ESP_LOGE(tag, "CP/M-86 guest is already initialized");
    return ESP_FAIL;
  }
  memset(&diskController, 0, sizeof(diskController));
  cpm86DriveConfig driveConfig[cpm86DiskDriveCount];
  char configError[128];
  cpm86DriveConfigResult configResult =
      cpm86DriveConfigLoad(driveConfigPath, driveConfig, configError, sizeof(configError));
  if (configResult == cpm86DriveConfigInvalid)
  {
    ESP_LOGW(tag, "%s; using the built-in A: system image only", configError);
  }
  else if (configResult == cpm86DriveConfigMissing && storageReady())
  {
    ESP_LOGW(tag, "%s", configError);
  }

  for (size_t drive = 0; drive < cpm86DiskDriveCount; ++drive)
  {
    if (!driveConfig[drive].configured)
    {
      continue;
    }
    uint64_t diskSize;
    if (drive == 0)
    {
      //-- The A: layout (small or large) follows from the size of the image file.
      char profileError[96];
      if (!resourceSize(driveConfig[0].path, &diskSize))
      {
        ESP_LOGE(tag, "Missing CP/M-86 system disk %s: A: is not mounted", driveConfig[0].path);
        closeDiskImages();
        return ESP_FAIL;
      }
      if (!cpm86DriveConfigResolveSystemProfile(&driveConfig[0], diskSize, profileError,
                                                sizeof(profileError)))
      {
        ESP_LOGE(tag, "Invalid CP/M-86 system disk %s: %s; A: is not mounted", driveConfig[0].path,
                 profileError);
        closeDiskImages();
        return ESP_FAIL;
      }
    }
    uint64_t expectedSize = cpm86SystemDiskSize;
    uint32_t tracks = cpm86SystemDiskTracks;
    uint8_t geometryCode = 0xFF;
    if (driveConfig[drive].profile == cpm86DiskProfileDataLarge ||
        driveConfig[drive].profile == cpm86DiskProfileSystemLarge)
    {
      expectedSize = cpm86LargeDiskSize;
      tracks = cpm86LargeDiskTracks;
      geometryCode = 0x01;
    }
    else if (driveConfig[drive].profile == cpm86DiskProfileDataBig)
    {
      expectedSize = cpm86BigDiskSize;
      tracks = cpm86BigDiskTracks;
      geometryCode = 0x02;
    }
    if (!resourceSize(driveConfig[drive].path, &diskSize) || diskSize != expectedSize)
    {
      if (drive == 0)
      {
        ESP_LOGE(tag, "Missing or invalid CP/M-86 system disk: %s", driveConfig[drive].path);
        closeDiskImages();
        return ESP_FAIL;
      }
      ESP_LOGW(tag, "CP/M-86 drive %c: image missing or has invalid size: %s", (char)('A' + drive),
               driveConfig[drive].path);
      continue;
    }
    if (!imageOpen(&diskController.drives[drive].image, driveConfig[drive].path,
                   driveConfig[drive].readOnly))
    {
      if (drive == 0)
      {
        ESP_LOGE(tag, "Could not open CP/M-86 system disk: %s", driveConfig[drive].path);
        closeDiskImages();
        return ESP_FAIL;
      }
      ESP_LOGW(tag, "Could not open CP/M-86 drive %c: image: %s", (char)('A' + drive),
               driveConfig[drive].path);
      continue;
    }
    diskController.drives[drive].tracks = tracks;
    diskController.drives[drive].geometryCode = geometryCode;
    diskController.drives[drive].configured = true;
  }
  if (!diskController.drives[0].configured)
  {
    ESP_LOGE(tag, "CP/M-86 A: system disk is unavailable: %s", systemDiskPath);
    return ESP_FAIL;
  }
  cpm86CoreConfig config = {
      .ramSize = 640 * 1024,
      .portRead = portRead,
      .portWrite = portWrite,
      .portContext = &diskController,
      .readMilliseconds = readMilliseconds,
      .clockContext = NULL,
  };
  cpm86CoreResult result = cpm86CoreCreate(&guestCore, &config);
  if (result != cpm86CoreOk)
  {
    if (result == cpm86CoreNoMemory)
    {
      ESP_LOGE(tag,
               "Could not allocate %u KiB CP/M-86 guest RAM; PSRAM free/largest=%u/%u bytes, "
               "internal free/largest=%u/%u bytes",
               (unsigned)(config.ramSize / 1024),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    else
    {
      ESP_LOGE(tag, "Could not create CP/M-86 CPU core: result=%d", result);
    }
    closeDiskImages();
    guestCore = NULL;
    return result == cpm86CoreNoMemory ? ESP_ERR_NO_MEM : ESP_FAIL;
  }
  if (!loadSystemFile())
  {
    cpm86CoreDestroy(guestCore);
    guestCore = NULL;
    closeDiskImages();
    return ESP_ERR_INVALID_STATE;
  }
  guestReady = true;
  return ESP_OK;
}

void cpm86MachineRun(void)
{
  if (!guestReady || guestCore == NULL)
  {
    puts("CP/M-86 guest is not initialized.");
    return;
  }

  //-- Discard stray menu input so it does not reach the guest console.
  while (hostConsoleCharAvailable())
  {
    (void)hostConsoleGetChar();
  }
  puts("\nStarting CP/M-86.");
  guestStopRequested = false;
  //-- Yield only after a time slice: with a 100 Hz tick every vTaskDelay(1) can sleep up
  //-- to 10 ms, so yielding per small batch would leave the CPU idle most of the time.
  const int64_t yieldSliceUs = 200000;
  int64_t sliceStartUs = esp_timer_get_time();
  while (true)
  {
    if (hostConsolePeekChar() == 0x1D)
    {
      (void)hostConsoleGetChar();
      guestStopRequested = true;
      break;
    }
    size_t instructionsExecuted = 0;
    cpm86CoreResult result = cpm86CoreRun(guestCore, 4096, &instructionsExecuted);
    if (guestStopRequested)
    {
      break;
    }
    if (result == cpm86CoreOk)
    {
      if (esp_timer_get_time() - sliceStartUs < yieldSliceUs)
      {
        continue;
      }
      vTaskDelay(1);
      sliceStartUs = esp_timer_get_time();
      continue;
    }
    if (result == cpm86CoreHalted)
    {
      ESP_LOGW(tag, "CP/M-86 guest halted after %llu instructions",
               (unsigned long long)cpm86CoreInstructionCount(guestCore));
      break;
    }
    uint16_t segment;
    uint16_t offset;
    uint8_t instructionBytes[6];
    uint32_t physicalAddress;
    if (cpm86CoreGetProgramCounter(guestCore, &segment, &offset) == cpm86CoreOk)
    {
      physicalAddress = (((uint32_t)segment << 4) + offset) & 0x000FFFFF;
      if (cpm86CoreRead(guestCore, physicalAddress, instructionBytes, sizeof(instructionBytes)) ==
          cpm86CoreOk)
      {
        ESP_LOGE(tag,
                 "CP/M-86 stopped: result=%d CS:IP=%04X:%04X physical=%05lX bytes=%02X %02X %02X "
                 "%02X %02X %02X "
                 "instructions=%llu executed=%u",
                 result, segment, offset, (unsigned long)physicalAddress, instructionBytes[0],
                 instructionBytes[1], instructionBytes[2], instructionBytes[3], instructionBytes[4],
                 instructionBytes[5], (unsigned long long)cpm86CoreInstructionCount(guestCore),
                 (unsigned)instructionsExecuted);
      }
      else
      {
        ESP_LOGE(tag, "CP/M-86 stopped: result=%d CS:IP=%04X:%04X instructions=%llu executed=%u",
                 result, segment, offset, (unsigned long long)cpm86CoreInstructionCount(guestCore),
                 (unsigned)instructionsExecuted);
      }
    }
    else
    {
      ESP_LOGE(tag, "CP/M-86 stopped: result=%d instructions=%llu executed=%u", result,
               (unsigned long long)cpm86CoreInstructionCount(guestCore),
               (unsigned)instructionsExecuted);
    }
    break;
  }

  if (guestStopRequested)
  {
    puts("CP/M-86 stopped by Ctrl+].");
  }
  cpm86CoreDestroy(guestCore);
  guestCore = NULL;
  guestReady = false;
  closeDiskImages();
}
