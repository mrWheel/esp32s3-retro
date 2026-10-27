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
#include "esp_psram.h"
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

enum
{
  reportedPortCapacity = 16
};

static uint16_t reportedPorts[reportedPortCapacity];
static size_t reportedPortCount;

//-- Tell the user once per port that the device does not exist. Returns false for the
//-- RETRO86_V1 BIOS port range (E0h-EFh) so misuse of those ports still stops the guest.
static bool reportUnavailableDevice(uint16_t port)
{
  if (port >= 0x00E0 && port <= 0x00EF)
  {
    return false;
  }
  for (size_t index = 0; index < reportedPortCount; ++index)
  {
    if (reportedPorts[index] == port)
    {
      return true;
    }
  }
  if (reportedPortCount < reportedPortCapacity)
  {
    reportedPorts[reportedPortCount++] = port;
  }
  printf("\r\nDevice 0x%04X not available.\r\n", port);
  return true;
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
  if (!reportUnavailableDevice(port))
  {
    return false;
  }
  *value = 0xFF;
  return true;
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
  //-- Writes to a non-existent device are ignored after a one-time notice.
  return reportUnavailableDevice(port);
}

enum
{
  pcBiosStubAddress = 0x0400,
  pcBiosInt16Offset = 0x2D
};

//-- Minimal guest-side stand-ins for the PC BIOS services some third-party CP/M-86 tools call:
//-- INT 1Ah AH=0 (tick count from the RETRO86_TIMER_B1 millisecond timer, ~18.2 Hz) and
//-- INT 16h AH=0/1 (keyboard read/status through the RETRO86 console ports E0h/E1h).
static const uint8_t pcBiosStubs[] = {
    0x80, 0xfc, 0x00, 0x75, 0x27, 0x53, 0x56, 0xe4, 0xf1, 0x88, 0xc3, 0xe4, 0xf2, 0x88,
    0xc7, 0xe4, 0xf3, 0x88, 0xc2, 0xe4, 0xf4, 0x88, 0xc6, 0xbe, 0x37, 0x00, 0x89, 0xd0,
    0x31, 0xd2, 0xf7, 0xf6, 0x89, 0xc1, 0x89, 0xd8, 0xf7, 0xf6, 0x89, 0xc2, 0x31, 0xc0,
    0x5e, 0x5b, 0xcf, 0x80, 0xfc, 0x01, 0x74, 0x0f, 0x08, 0xe4, 0x75, 0x0a, 0xe4, 0xe0,
    0x08, 0xc0, 0x74, 0xfa, 0xe4, 0xe1, 0x30, 0xe4, 0xcf, 0xe4, 0xe0, 0x08, 0xc0, 0x74,
    0x06, 0xb8, 0x01, 0x00, 0xca, 0x02, 0x00, 0x31, 0xc0, 0xca, 0x02, 0x00};

static bool installPcBiosStubs(void)
{
  const uint8_t vectors[2][4] = {{pcBiosStubAddress & 0xFF, pcBiosStubAddress >> 8, 0, 0},
                                 {(pcBiosStubAddress + pcBiosInt16Offset) & 0xFF,
                                  (pcBiosStubAddress + pcBiosInt16Offset) >> 8, 0, 0}};
  return cpm86CoreLoad(guestCore, pcBiosStubAddress, pcBiosStubs, sizeof(pcBiosStubs)) ==
             cpm86CoreOk &&
         cpm86CoreLoad(guestCore, 0x1A * 4, vectors[0], 4) == cpm86CoreOk &&
         cpm86CoreLoad(guestCore, 0x16 * 4, vectors[1], 4) == cpm86CoreOk;
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

//-- Log a full fault dump so a stop can be pinned to the faulting instruction, the register
//-- state, and the specific access (physical address or I/O port) that went wrong.
static void logCpm86Stop(cpm86CoreResult result, size_t instructionsExecuted)
{
  cpm86CoreFaultInfo fault;
  if (cpm86CoreGetFaultInfo(guestCore, &fault) != cpm86CoreOk)
  {
    ESP_LOGE(tag, "CP/M-86 stopped: result=%d executed=%u", result, (unsigned)instructionsExecuted);
    return;
  }
  uint32_t startPhysical = (((uint32_t)fault.cs << 4) + fault.startIp) & 0x000FFFFF;
  uint8_t startBytes[6];
  char startText[48];
  if (cpm86CoreRead(guestCore, startPhysical, startBytes, sizeof(startBytes)) == cpm86CoreOk)
  {
    snprintf(startText, sizeof(startText), "%02X %02X %02X %02X %02X %02X", startBytes[0],
             startBytes[1], startBytes[2], startBytes[3], startBytes[4], startBytes[5]);
  }
  else
  {
    snprintf(startText, sizeof(startText), "(unreadable)");
  }
  ESP_LOGE(tag, "CP/M-86 stopped: result=%d at %04X:%04X (phys %05lX) bytes: %s", result, fault.cs,
           fault.startIp, (unsigned long)startPhysical, startText);
  ESP_LOGE(tag, "CP/M-86 stopped: post-fault IP %04X:%04X; segs ES=%04X CS=%04X SS=%04X DS=%04X",
           fault.cs, fault.ip, fault.es, fault.cs, fault.ss, fault.ds);
  ESP_LOGE(tag,
           "CP/M-86 stopped: regs AX=%04X CX=%04X DX=%04X BX=%04X SP=%04X BP=%04X SI=%04X DI=%04X "
           "FLAGS=%04X instructions=%llu executed=%u",
           fault.ax, fault.cx, fault.dx, fault.bx, fault.sp, fault.bp, fault.si, fault.di,
           fault.flags, (unsigned long long)cpm86CoreInstructionCount(guestCore),
           (unsigned)instructionsExecuted);
  if (result == cpm86CoreMemoryFault)
  {
    ESP_LOGE(tag, "CP/M-86 stopped: memory %s at physical %05lX", fault.memWrite ? "write" : "read",
             (unsigned long)fault.memAddr);
  }
  else if (result == cpm86CoreIoError)
  {
    ESP_LOGE(tag, "CP/M-86 stopped: I/O %s port %04X value %02X", fault.ioRead ? "IN" : "OUT",
             fault.ioPort, fault.ioValue);
  }
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
  reportedPortCount = 0;
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
  ESP_LOGI(tag,
           "PSRAM size=%u bytes, free=%u bytes, largest block=%u bytes; guest RAM=%u KiB allocated",
           (unsigned)esp_psram_get_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
           (unsigned)(config.ramSize / 1024));
  if (!loadSystemFile() || !installPcBiosStubs())
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
    logCpm86Stop(result, instructionsExecuted);
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
