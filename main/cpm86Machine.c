#include "cpm86Machine.h"
#include "cpm86Core.h"
#include "hostConsole.h"
#include "imageFile.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *tag = "cpm86Machine";
static const char *systemFilePath = "/littlefs/cpm86/cpm.sys";
static const char *systemDiskPath = "/littlefs/cpm86/system.dsk";

typedef enum
{
  diskTransferIdle,
  diskTransferRead,
  diskTransferWrite
} diskTransferMode;

typedef struct
{
  imageFile image;
  uint8_t record[128];
  uint16_t track;
  uint16_t sector;
  uint8_t drive;
  size_t transferIndex;
  uint8_t transferStatus;
  diskTransferMode transferMode;
} cpm86Disk;

static cpm86Core *guestCore;
static cpm86Disk systemDisk;
static bool guestReady;

static bool diskRecordOffset(uint64_t *offset)
{
  if (offset == NULL || systemDisk.drive != 0 || systemDisk.track == 0 || systemDisk.track >= 40 ||
      systemDisk.sector >= 32)
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)systemDisk.track * 32 + systemDisk.sector;
  *offset = recordIndex * sizeof(systemDisk.record);
  return *offset <= imageSize(&systemDisk.image) &&
         sizeof(systemDisk.record) <= imageSize(&systemDisk.image) - *offset;
}

static bool portRead(void *context, uint16_t port, uint8_t *value)
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
    *value = (uint8_t)character;
    return true;
  }
  if (port == 0x00ED)
  {
    *value = systemDisk.transferStatus;
    return true;
  }
  if (port == 0x00EE && systemDisk.transferMode == diskTransferRead &&
      systemDisk.transferIndex < sizeof(systemDisk.record))
  {
    *value = systemDisk.record[systemDisk.transferIndex++];
    return true;
  }
  return false;
}

static bool startDiskTransfer(uint8_t command)
{
  uint64_t offset;
  systemDisk.transferIndex = 0;
  systemDisk.transferStatus = 1;
  systemDisk.transferMode = diskTransferIdle;
  if (!diskRecordOffset(&offset))
  {
    return true;
  }

  if (command == 0)
  {
    if (!imageReadAt(&systemDisk.image, offset, systemDisk.record, sizeof(systemDisk.record)))
    {
      return true;
    }
    systemDisk.transferStatus = 0;
    systemDisk.transferMode = diskTransferRead;
  }
  else if (command == 1)
  {
    systemDisk.transferStatus = 0;
    systemDisk.transferMode = diskTransferWrite;
  }
  return true;
}

static bool portWrite(void *context, uint16_t port, uint8_t value)
{
  (void)context;
  if (port == 0x00E2)
  {
    hostConsolePutChar((char)value);
    return true;
  }
  if (port == 0x00E8)
  {
    systemDisk.drive = value;
    return true;
  }
  if (port == 0x00E9)
  {
    systemDisk.track = (systemDisk.track & 0xFF00) | value;
    return true;
  }
  if (port == 0x00EA)
  {
    systemDisk.track = (systemDisk.track & 0x00FF) | ((uint16_t)value << 8);
    return true;
  }
  if (port == 0x00EB)
  {
    systemDisk.sector = (systemDisk.sector & 0xFF00) | value;
    return true;
  }
  if (port == 0x00EC)
  {
    systemDisk.sector = (systemDisk.sector & 0x00FF) | ((uint16_t)value << 8);
    return true;
  }
  if (port == 0x00ED)
  {
    return startDiskTransfer(value);
  }
  if (port == 0x00EE && systemDisk.transferMode == diskTransferWrite &&
      systemDisk.transferIndex < sizeof(systemDisk.record))
  {
    systemDisk.record[systemDisk.transferIndex++] = value;
    if (systemDisk.transferIndex == sizeof(systemDisk.record))
    {
      uint64_t offset;
      if (!diskRecordOffset(&offset) ||
          !imageWriteAt(&systemDisk.image, offset, systemDisk.record, sizeof(systemDisk.record)) ||
          !imageFlush(&systemDisk.image))
      {
        systemDisk.transferStatus = 1;
      }
      systemDisk.transferMode = diskTransferIdle;
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
               imageReadAt(&image, 0, header, sizeof(header)) &&
               header[0] == 1 && header[3] == 0x51 && header[4] == 0;
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

  //-- CCP data/stack area (0051:0800-09FF) is never code; stop at the first fetch to keep the real cause in the trace.
  cpm86CoreSetExecuteGuard(guestCore, 0x00D10, 0x00F10);
  const uint8_t bdosVector[] = {0x06, 0x0B, 0x51, 0x00};
  if (cpm86CoreWrite(guestCore, 0x00380, bdosVector, sizeof(bdosVector)) != cpm86CoreOk ||
      cpm86CoreSetEntry(guestCore, 0x0051, 0x2500) != cpm86CoreOk)
  {
    ESP_LOGE(tag, "Could not initialize CP/M-86 entry state");
    return false;
  }
  return true;
}

static void closeSystemDisk(void)
{
  if (systemDisk.image.file != NULL && !imageClose(&systemDisk.image))
  {
    ESP_LOGE(tag, "Failed to close CP/M-86 system disk");
  }
  memset(&systemDisk, 0, sizeof(systemDisk));
}

machineState cpm86MachineProbe(const retroMachine *machine)
{
  if (machine == NULL || !machine->implemented)
  {
    return machineNotImplemented;
  }
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
  uint64_t diskSize;
  if (!resourceSize(systemDiskPath, &diskSize))
  {
    return machineMissingResource;
  }
  if (diskSize != cpm86SystemDiskSize)
  {
    ESP_LOGE(tag, "Invalid CP/M-86 system disk size: %s", systemDiskPath);
    return machineResourceInvalid;
  }
  return machineAvailable;
}

esp_err_t cpm86MachineInitialize(void)
{
  if (guestReady || guestCore != NULL)
  {
    ESP_LOGE(tag, "CP/M-86 guest is already initialized");
    return ESP_FAIL;
  }
  memset(&systemDisk, 0, sizeof(systemDisk));
  if (!imageOpen(&systemDisk.image, systemDiskPath, false))
  {
    ESP_LOGE(tag, "Could not open writable CP/M-86 system disk: %s", systemDiskPath);
    return ESP_FAIL;
  }
  cpm86CoreConfig config = {
      .ramSize = 640 * 1024,
      .portRead = portRead,
      .portWrite = portWrite,
      .portContext = &systemDisk,
  };
  cpm86CoreResult result = cpm86CoreCreate(&guestCore, NULL, &config);
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
    closeSystemDisk();
    guestCore = NULL;
    return result == cpm86CoreNoMemory ? ESP_ERR_NO_MEM : ESP_FAIL;
  }
  if (!loadSystemFile())
  {
    cpm86CoreDestroy(guestCore);
    guestCore = NULL;
    closeSystemDisk();
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
  puts("\nStarting CP/M-86 on the virtual A: disk.");
  while (true)
  {
    size_t instructionsExecuted;
    cpm86CoreResult result = cpm86CoreRun(guestCore, 4096, &instructionsExecuted);
    if (result == cpm86CoreOk)
    {
      vTaskDelay(1);
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
      cpm86CoreTraceEntry trace[cpm86CoreTraceDepth];
      size_t traceCount = 0;
      static cpm86CoreTraceEntry head[cpm86CoreHeadDepth];
      size_t headCount = 0;
      if (cpm86CoreGetHeadTrace(guestCore, head, cpm86CoreHeadDepth, &headCount) == cpm86CoreOk)
      {
        for (size_t index = 0; index < headCount; ++index)
        {
          ESP_LOGE(tag, "First guest instruction %u: %04X:%04X SS:SP=%04X:%04X AX=%04X %s", (unsigned)index,
                   head[index].segment, head[index].offset, head[index].stackSegment, head[index].stackPointer,
                   head[index].accumulator, head[index].instruction);
        }
      }
      uint8_t dataBytes[16];
      if (cpm86CoreRead(guestCore, 0x00D10, dataBytes, sizeof(dataBytes)) == cpm86CoreOk)
      {
        ESP_LOG_BUFFER_HEX_LEVEL(tag, dataBytes, sizeof(dataBytes), ESP_LOG_ERROR);
      }
      if (cpm86CoreGetRecentTrace(guestCore, trace, cpm86CoreTraceDepth, &traceCount) == cpm86CoreOk)
      {
        for (size_t index = 0; index < traceCount; ++index)
        {
          ESP_LOGE(tag, "Recent guest instruction %u: %04X:%04X SS:SP=%04X:%04X AX=%04X %s",
                   (unsigned)index, trace[index].segment, trace[index].offset, trace[index].stackSegment,
                   trace[index].stackPointer, trace[index].accumulator, trace[index].instruction);
        }
      }
      if (cpm86CoreRead(guestCore, physicalAddress, instructionBytes, sizeof(instructionBytes)) == cpm86CoreOk)
      {
        ESP_LOGE(tag,
                 "CP/M-86 stopped: result=%d CS:IP=%04X:%04X physical=%05lX bytes=%02X %02X %02X %02X %02X %02X "
                 "instructions=%llu executed=%u",
                 result, segment, offset, (unsigned long)physicalAddress, instructionBytes[0], instructionBytes[1],
                 instructionBytes[2], instructionBytes[3], instructionBytes[4], instructionBytes[5],
                 (unsigned long long)cpm86CoreInstructionCount(guestCore), (unsigned)instructionsExecuted);
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
      ESP_LOGE(tag, "CP/M-86 stopped: result=%d instructions=%llu executed=%u",
               result, (unsigned long long)cpm86CoreInstructionCount(guestCore), (unsigned)instructionsExecuted);
    }
    break;
  }

  cpm86CoreDestroy(guestCore);
  guestCore = NULL;
  guestReady = false;
  closeSystemDisk();
}
