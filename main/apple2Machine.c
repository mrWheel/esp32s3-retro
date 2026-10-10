#include "apple2Machine.h"
#include "apple2Core.h"
#include "apple2DiskImage.h"
#include "apple2DriveConfig.h"
#include "diskActivity.h"
#include "hostConsole.h"
#include "storage.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *tag = "apple2Machine";
static const char *romPath = "/littlefs/apple2/apple2.rom";
static const char *driveConfigPath = "/microSD/retro/images/apple2/drives.cfg";
static const size_t bootSlotIndex = 6 - apple2DriveConfigFirstSlot;
static apple2Core *guestCore;
static apple2DiskImage guestDisks[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot];
static uint8_t renderedCells[apple2TextRows][apple2VidexTextColumns];
static bool screenRendered;
static bool renderedFlashOn;
static bool renderedVidexMode;
static bool renderedVidexModeKnown;

static bool appendText(char *buffer, size_t capacity, size_t *length, const char *text)
{
  size_t textLength = strlen(text);
  if (textLength >= capacity - *length)
  {
    return false;
  }
  memcpy(&buffer[*length], text, textLength);
  *length += textLength;
  return true;
}

static bool appendCharacter(char *buffer, size_t capacity, size_t *length, char character)
{
  if (*length + 1 >= capacity)
  {
    return false;
  }
  buffer[(*length)++] = character;
  return true;
}

static char decodeVidexCharacter(uint8_t value)
{
  value &= 0x7F;
  return value >= 0x20 ? (char)value : ' ';
}

static bool appendTextCell(char *buffer, size_t capacity, size_t *length, const apple2Core *core, uint8_t value,
                           bool flashOn, bool *inverse, bool *flashing)
{
  bool cellInverse = (value & 0x80) == 0 && (value & 0x40) == 0;
  bool cellFlashing = (value & 0xC0) == 0x40;
  if (cellInverse != *inverse)
  {
    if (!appendText(buffer, capacity, length, cellInverse ? "\x1b[7m" : "\x1b[27m"))
    {
      return false;
    }
    *inverse = cellInverse;
  }
  if (cellFlashing != *flashing)
  {
    if (!appendText(buffer, capacity, length, cellFlashing ? "\x1b[5m" : "\x1b[25m"))
    {
      return false;
    }
    *flashing = cellFlashing;
  }
  char character = cellFlashing && !flashOn ? ' ' : apple2CoreDecodeTextCharacter(core, value);
  return appendCharacter(buffer, capacity, length, character);
}

static bool appendVidexTextCell(char *buffer, size_t capacity, size_t *length, uint8_t value, bool *inverse)
{
  bool cellInverse = (value & 0x80) != 0;
  if (cellInverse != *inverse)
  {
    if (!appendText(buffer, capacity, length, cellInverse ? "\x1b[7m" : "\x1b[27m"))
    {
      return false;
    }
    *inverse = cellInverse;
  }
  return appendCharacter(buffer, capacity, length, decodeVidexCharacter(value));
}

static void renderScreen(int64_t nowUs)
{
  apple2VideoState video;
  apple2CoreGetVideoState(guestCore, &video);
  size_t columns = video.videxTextMode ? apple2VidexTextColumns : apple2TextColumns;
  if (renderedVidexModeKnown && video.videxTextMode != renderedVidexMode)
  {
    if (!hostConsoleWrite("\x1b[2J\x1b[H"))
    {
      return;
    }
    screenRendered = false;
  }
  bool flashOn = (nowUs / 500000) % 2 == 0;
  for (size_t row = 0; row < apple2TextRows; ++row)
  {
    bool showText = video.textMode || (video.mixedMode && row >= 20);
    uint8_t currentCells[apple2VidexTextColumns] = {0};
    bool hasFlashingCells = false;
    for (size_t column = 0; column < columns; ++column)
    {
      uint8_t value = 0xA0;
      if (video.videxTextMode)
      {
        if (apple2CoreReadVidexTextCell(guestCore, row, column, &value) != apple2CoreOk)
        {
          return;
        }
      }
      else if (showText && apple2CoreReadTextCell(guestCore, video.page2, row, column, &value) != apple2CoreOk)
      {
        return;
      }
      if (!video.videxTextMode && !showText && row == 0 &&
          column < sizeof("GRAPHICS DISPLAY NOT IMPLEMENTED") - 1)
      {
        value = (uint8_t)("GRAPHICS DISPLAY NOT IMPLEMENTED"[column] | 0x80);
      }
      currentCells[column] = value;
      hasFlashingCells = hasFlashingCells || (!video.videxTextMode && (value & 0xC0) == 0x40);
    }
    bool cellsChanged = !screenRendered || memcmp(currentCells, renderedCells[row], columns) != 0;
    bool flashChanged = screenRendered && hasFlashingCells && flashOn != renderedFlashOn;
    if (cellsChanged || flashChanged)
    {
      char rowBuffer[1024];
      size_t length = 0;
      bool inverse = false;
      bool flashing = false;
      char cursor[16];
      int cursorLength = snprintf(cursor, sizeof(cursor), "\x1b[%zu;1H", row + 1);
      if (cursorLength < 0 || (size_t)cursorLength >= sizeof(cursor) ||
          !appendText(rowBuffer, sizeof(rowBuffer), &length, cursor))
      {
        return;
      }
      for (size_t column = 0; column < columns; ++column)
      {
        bool appended = video.videxTextMode
                            ? appendVidexTextCell(rowBuffer, sizeof(rowBuffer), &length, currentCells[column],
                                                  &inverse)
                            : appendTextCell(rowBuffer, sizeof(rowBuffer), &length, guestCore,
                                             currentCells[column], flashOn, &inverse, &flashing);
        if (!appended)
        {
          return;
        }
      }
      if (!appendText(rowBuffer, sizeof(rowBuffer), &length, "\x1b[0m"))
      {
        return;
      }
      rowBuffer[length] = '\0';
      if (!hostConsoleWrite(rowBuffer))
      {
        return;
      }
      memcpy(renderedCells[row], currentCells, sizeof(currentCells));
    }
  }
  size_t cursorRow;
  size_t cursorColumn;
  bool cursorAvailable = video.videxTextMode
                             ? apple2CoreGetVidexCursor(guestCore, &cursorRow, &cursorColumn)
                             : apple2CoreGetTextCursor(guestCore, &cursorRow, &cursorColumn);
  if (cursorAvailable)
  {
    size_t maxRows = video.videxTextMode ? apple2VidexTextRows : apple2TextRows;
    if (cursorRow >= maxRows)
    {
      cursorRow = maxRows - 1;
    }
    if (cursorColumn >= columns)
    {
      cursorColumn = columns - 1;
    }
    char cursor[32];
    int cursorLength = snprintf(cursor, sizeof(cursor), "\x1b[?25h\x1b[%zu;%zuH", cursorRow + 1,
                                cursorColumn + 1);
    if (cursorLength < 0 || (size_t)cursorLength >= sizeof(cursor) || !hostConsoleWrite(cursor))
    {
      return;
    }
  }
  screenRendered = true;
  renderedFlashOn = flashOn;
  renderedVidexMode = video.videxTextMode;
  renderedVidexModeKnown = true;
}

static void handleInput(bool *skipLineFeed)
{
  if (apple2CoreKeyPending(guestCore) || !hostConsoleCharAvailable())
  {
    return;
  }
  int character = hostConsoleGetChar();
  if (character < 0)
  {
    return;
  }
  if (character == '\n' && *skipLineFeed)
  {
    *skipLineFeed = false;
    return;
  }
  *skipLineFeed = false;
  if (character == '\r')
  {
    *skipLineFeed = true;
    character = '\r';
  }
  else if (character == '\n')
  {
    character = '\r';
  }
  else if (character == 8 || character == 127)
  {
    character = 8;
  }
  if (character >= 0 && character <= 0x7F)
  {
    apple2CorePressKey(guestCore, (uint8_t)character);
  }
}

machineState apple2MachineProbe(const retroMachine *machine)
{
  (void)machine;
  struct stat info;
  if (stat(romPath, &info) != 0)
  {
    return machineMissingResource;
  }
  if (!S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size != apple2RomSize)
  {
    return machineResourceInvalid;
  }
  FILE *romFile = fopen(romPath, "rb");
  if (romFile == NULL || fseek(romFile, 0x2FFC, SEEK_SET) != 0)
  {
    if (romFile != NULL)
    {
      fclose(romFile);
    }
    return machineResourceInvalid;
  }
  uint8_t resetVector[2];
  bool valid = fread(resetVector, 1, sizeof(resetVector), romFile) == sizeof(resetVector) && !ferror(romFile);
  if (fclose(romFile) != 0)
  {
    return machineResourceInvalid;
  }
  uint16_t resetAddress = valid ? (uint16_t)(resetVector[0] | ((uint16_t)resetVector[1] << 8)) : 0;
  return valid && resetAddress >= 0xD000 ? machineAvailable : machineResourceInvalid;
}

static bool readDiskSector(void *context, uint8_t track, uint8_t sector, uint8_t *buffer)
{
  diskActivityRead();
  return apple2DiskImageReadSectorCallback(context, track, sector, buffer);
}

static bool writeDiskSector(void *context, uint8_t track, uint8_t sector, const uint8_t *buffer)
{
  return apple2DiskImageWriteSectorCallback(context, track, sector, buffer);
}

static bool readSmartPortBlock(void *context, uint32_t block, uint8_t *buffer)
{
  diskActivityRead();
  bool read = apple2DiskImageReadBlockCallback(context, block, buffer);
  if (!read)
  {
    ESP_LOGE(tag, "SmartPort block %lu read failed", (unsigned long)block);
  }
  return read;
}

static bool writeSmartPortBlock(void *context, uint32_t block, const uint8_t *buffer)
{
  diskActivityWrite();
  bool written = apple2DiskImageWriteBlockCallback(context, block, buffer);
  if (!written)
  {
    ESP_LOGE(tag, "SmartPort block %lu write failed", (unsigned long)block);
  }
  return written;
}

static void releaseDisk(void)
{
  if (guestCore != NULL)
  {
    apple2CoreDetachDisk(guestCore);
  }
  for (size_t slotIndex = 0; slotIndex < apple2DriveConfigSlotCount; ++slotIndex)
  {
    for (size_t driveIndex = 0; driveIndex < apple2DriveConfigDrivesPerSlot; ++driveIndex)
    {
      apple2DiskImage *image = &guestDisks[slotIndex][driveIndex];
      if (apple2DiskImageIsOpen(image) && apple2DiskImageClose(image) != apple2DiskImageOk)
      {
        ESP_LOGW(tag, "closing the image of PR%u.%u failed", (unsigned)(slotIndex + apple2DriveConfigFirstSlot),
                 (unsigned)(driveIndex + 1));
      }
      apple2DiskImageInitialize(image);
    }
  }
}

static const char *contentText(const apple2DiskImage *image)
{
  switch (image->probe.content)
  {
  case apple2DiskImageContentPascal:
    return "Apple Pascal";
  case apple2DiskImageContentProDos:
    return "ProDOS";
  case apple2DiskImageContentUnknown:
    break;
  }
  return "unrecognised content";
}

typedef enum
{
  driveModeDiskII,
  driveModeSmartPort,
  driveModeProDosBlock
} driveMode;

//-- A missing or invalid disk image is reported but never prevents the machine from starting.
static bool openDriveImage(size_t slotIndex, size_t driveIndex, const apple2DriveConfig *config)
{
  uint8_t slot = (uint8_t)(slotIndex + apple2DriveConfigFirstSlot);
  apple2DiskImage *image = &guestDisks[slotIndex][driveIndex];
  apple2DiskImageInitialize(image);
  apple2DiskImageResult result = apple2DiskImageOpenProfileMode(image, config->path, config->profile,
                                                                apple2DiskImageOrderAuto, config->readOnly);
  if (result != apple2DiskImageOk)
  {
    ESP_LOGW(tag, "no disk media on %s%u.%u: %s: %s", config->smartPort ? "SP" : "SD", (unsigned)slot,
             (unsigned)(driveIndex + 1), config->path, apple2DiskImageResultText(result));
    return false;
  }
  return true;
}

static void attachOpenedDrive(size_t slotIndex, size_t driveIndex, const apple2DriveConfig *config, driveMode mode)
{
  uint8_t slot = (uint8_t)(slotIndex + apple2DriveConfigFirstSlot);
  apple2DiskImage *image = &guestDisks[slotIndex][driveIndex];
  apple2DiskSectorOrder order = image->order == apple2DiskImageOrderProdos ? apple2DiskSectorOrderProdos
                                                                            : apple2DiskSectorOrderDos;
  const char *prefix = mode == driveModeSmartPort ? "SP" : "SD";
  apple2CoreResult attachResult;
  if (mode == driveModeDiskII)
  {
    attachResult = apple2CoreAttachWritableDiskDrive(guestCore, slot, (uint8_t)driveIndex, order, image->trackCount,
                                                     readDiskSector, config->readOnly ? NULL : writeDiskSector, image);
  }
  else
  {
    attachResult = apple2CoreAttachBlockDevice(
        guestCore, slot, (uint8_t)(driveIndex + 1), (uint32_t)image->trackCount * 8U,
        mode == driveModeProDosBlock, readSmartPortBlock, config->readOnly ? NULL : writeSmartPortBlock, image);
  }
  if (attachResult != apple2CoreOk)
  {
    ESP_LOGE(tag, "attaching %s to %s%u.%u failed", config->path, prefix, (unsigned)slot, (unsigned)(driveIndex + 1));
    apple2DiskImageClose(image);
    apple2DiskImageInitialize(image);
    return;
  }
  if (mode == driveModeSmartPort)
  {
    ESP_LOGI(tag, "SmartPort SP%u.%u (%s): %s, %u blocks, %s", (unsigned)slot,
             (unsigned)(driveIndex + 1), config->readOnly ? "read-only" : "read/write", config->path,
             (unsigned)image->trackCount * 8U, contentText(image));
  }
  else if (mode == driveModeProDosBlock)
  {
    ESP_LOGI(tag, "ProDOS block device PR%u.%u (%s): %s, %u blocks, boots with PR#%u", (unsigned)slot,
             (unsigned)(driveIndex + 1), config->readOnly ? "read-only" : "read/write", config->path,
             (unsigned)image->trackCount * 8U, (unsigned)slot);
  }
  else
  {
    ESP_LOGI(tag, "Disk II PR%u.%u (%s): %s, %u tracks, %s sector order, %s", (unsigned)slot,
             (unsigned)(driveIndex + 1), config->readOnly ? "read-only" : "read/write", config->path,
             (unsigned)image->trackCount,
             image->order == apple2DiskImageOrderProdos ? "ProDOS/Pascal" : "DOS", contentText(image));
  }
  if (image->probe.content == apple2DiskImageContentPascal || image->probe.content == apple2DiskImageContentProDos)
  {
    ESP_LOGI(tag, "%s%u.%u %s volume %s: %u blocks", prefix, (unsigned)slot,
             (unsigned)(driveIndex + 1), image->probe.content == apple2DiskImageContentProDos ? "ProDOS" : "Apple Pascal",
             image->probe.volumeName, (unsigned)image->probe.volumeBlocks);
  }
  if (mode == driveModeDiskII && image->orderSuspect)
  {
    ESP_LOGW(tag, "PR%u.%u: the sector order chosen from the file extension disagrees with the content (%s expected)",
             (unsigned)slot, (unsigned)(driveIndex + 1),
             image->probe.order == apple2DiskImageOrderProdos ? "ProDOS/Pascal" : "DOS");
  }
}

//-- A controller slot is a SmartPort interface (SP5.x), a ProDOS block device when one of its images holds a ProDOS
//-- volume (the Disk II boot path cannot start ProDOS), or a Disk II controller for every other content.
//-- The boot drive SD6.1 must be configured in drives.cfg and its image must open; otherwise the machine does not start.
static esp_err_t attachDisks(void)
{
  apple2DriveConfig config[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot];
  char configError[128];
  apple2DriveConfigResult configResult = apple2DriveConfigLoad(driveConfigPath, config, configError, sizeof(configError));
  bool bootDriveOpened = false;
  if (configResult != apple2DriveConfigLoaded)
  {
    ESP_LOGE(tag, "%s; no Apple II drives are attached", configError);
    printf("Apple II: %s; no drives are attached.\n", configError);
  }
  for (size_t slotIndex = 0; slotIndex < apple2DriveConfigSlotCount; ++slotIndex)
  {
    bool opened[apple2DriveConfigDrivesPerSlot] = {false};
    bool proDosContent = false;
    bool smartPort = false;
    for (size_t driveIndex = 0; driveIndex < apple2DriveConfigDrivesPerSlot; ++driveIndex)
    {
      if (config[slotIndex][driveIndex].configured)
      {
        opened[driveIndex] = openDriveImage(slotIndex, driveIndex, &config[slotIndex][driveIndex]);
        if (slotIndex == bootSlotIndex && driveIndex == 0)
        {
          bootDriveOpened = opened[driveIndex];
        }
        smartPort = smartPort || config[slotIndex][driveIndex].smartPort;
        proDosContent = proDosContent ||
                        (opened[driveIndex] &&
                         guestDisks[slotIndex][driveIndex].probe.content == apple2DiskImageContentProDos);
      }
    }
    driveMode mode = smartPort ? driveModeSmartPort : (proDosContent ? driveModeProDosBlock : driveModeDiskII);
    for (size_t driveIndex = 0; driveIndex < apple2DriveConfigDrivesPerSlot; ++driveIndex)
    {
      if (opened[driveIndex])
      {
        attachOpenedDrive(slotIndex, driveIndex, &config[slotIndex][driveIndex], mode);
      }
    }
  }
  if (bootDriveOpened)
  {
    return ESP_OK;
  }
  if (!config[bootSlotIndex][0].configured)
  {
    ESP_LOGE(tag, "boot drive SD6.1 is not configured in %s", driveConfigPath);
    printf("Apple II cannot start: boot drive SD6.1 is not configured in %s.\n"
           "Add a line such as SD6.1=/littlefs/apple2/system.dsk,RO,APPLE2_140K\n",
           driveConfigPath);
  }
  else
  {
    ESP_LOGE(tag, "boot drive SD6.1 image %s cannot be opened", config[bootSlotIndex][0].path);
    printf("Apple II cannot start: the SD6.1 image %s cannot be opened.\n", config[bootSlotIndex][0].path);
  }
  return ESP_ERR_NOT_FOUND;
}

//-- Counts sector writes and write errors over all controllers.
static uint32_t totalWriteAttempts(void)
{
  uint32_t total = 0;
  for (uint8_t slot = apple2DriveConfigFirstSlot; slot <= apple2DriveConfigLastSlot; ++slot)
  {
    apple2DiskState state;
    if (apple2CoreGetDiskStateForSlot(guestCore, slot, &state))
    {
      total += state.writeAttempts;
    }
  }
  return total;
}

static uint32_t totalWriteFailures(void)
{
  uint32_t total = 0;
  for (uint8_t slot = apple2DriveConfigFirstSlot; slot <= apple2DriveConfigLastSlot; ++slot)
  {
    apple2DiskState state;
    if (apple2CoreGetDiskStateForSlot(guestCore, slot, &state))
    {
      total += state.writeFailures;
    }
  }
  return total;
}

esp_err_t apple2MachineInitialize(void)
{
  FILE *romFile = fopen(romPath, "rb");
  if (romFile == NULL)
  {
    return ESP_ERR_NOT_FOUND;
  }
  uint8_t *rom = malloc(apple2RomSize);
  if (rom == NULL)
  {
    fclose(romFile);
    return ESP_ERR_NO_MEM;
  }
  bool loaded = fread(rom, 1, apple2RomSize, romFile) == apple2RomSize && fgetc(romFile) == EOF && !ferror(romFile);
  bool closed = fclose(romFile) == 0;
  if (!loaded || !closed)
  {
    free(rom);
    return ESP_ERR_INVALID_SIZE;
  }
  if (guestCore != NULL)
  {
    releaseDisk();
    apple2CoreDestroy(guestCore);
    guestCore = NULL;
  }
  memset(renderedCells, 0, sizeof(renderedCells));
  screenRendered = false;
  renderedFlashOn = false;
  renderedVidexMode = false;
  renderedVidexModeKnown = false;
  apple2CoreResult coreResult = apple2CoreCreate(&guestCore);
  if (coreResult != apple2CoreOk)
  {
    free(rom);
    return coreResult == apple2CoreNoMemory ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
  }
  coreResult = apple2CoreSetCharacterOptions(guestCore, true, true);
  if (coreResult == apple2CoreOk)
  {
    //-- The 80-column card is selected automatically at every start, like the Disk II controller boots.
    coreResult = apple2CoreSetBootIn80Columns(guestCore, true);
  }
  if (coreResult != apple2CoreOk)
  {
    apple2CoreDestroy(guestCore);
    guestCore = NULL;
    free(rom);
    return ESP_ERR_INVALID_STATE;
  }
  coreResult = apple2CoreLoadRom(guestCore, rom, apple2RomSize);
  free(rom);
  if (coreResult != apple2CoreOk)
  {
    apple2CoreDestroy(guestCore);
    guestCore = NULL;
    return ESP_ERR_INVALID_RESPONSE;
  }
  esp_err_t attachResult = attachDisks();
  if (attachResult != ESP_OK)
  {
    releaseDisk();
    apple2CoreDestroy(guestCore);
    guestCore = NULL;
  }
  return attachResult;
}

void apple2MachineRun(void)
{
  if (guestCore == NULL)
  {
    puts("Apple II initialization is incomplete.");
    return;
  }
  screenRendered = false;
  hostConsoleWrite("\x1b[2J\x1b[H\x1b[?25h");
  TickType_t wakeTime = xTaskGetTickCount();
  TickType_t period = pdMS_TO_TICKS(1);
  if (period == 0)
  {
    period = 1;
  }
  size_t cyclesPerPeriod = (size_t)(((uint64_t)1023000 * period + configTICK_RATE_HZ - 1) /
                                    configTICK_RATE_HZ);
  bool skipLineFeed = hostConsolePeekChar() == '\n';
  int64_t lastRenderUs = 0;
  uint32_t seenWriteAttempts = totalWriteAttempts();
  uint32_t seenWriteFailures = totalWriteFailures();
  while (true)
  {
    handleInput(&skipLineFeed);
    if (apple2CoreRunCycles(guestCore, cyclesPerPeriod) != apple2CoreOk)
    {
      ESP_LOGE(tag, "6502 execution stopped");
      releaseDisk();
      return;
    }
    //-- Green marks reads; red marks attempted writes.
    uint32_t writeAttempts = totalWriteAttempts();
    if (writeAttempts != seenWriteAttempts)
    {
      seenWriteAttempts = writeAttempts;
#if DISK_ACTIVITY_DEBUG_LOG
      ESP_LOGD(tag, "Disk II sector write #%u", (unsigned)writeAttempts);
#endif
      diskActivityWrite();
    }
    uint32_t writeFailures = totalWriteFailures();
    if (writeFailures != seenWriteFailures)
    {
      seenWriteFailures = writeFailures;
      ESP_LOGW(tag, "Disk II sector write failed #%u", (unsigned)writeFailures);
    }
    TickType_t currentTime = xTaskGetTickCount();
    if ((TickType_t)(currentTime - wakeTime) >= period)
    {
      vTaskDelay(1);
      wakeTime = xTaskGetTickCount();
    }
    else
    {
      vTaskDelayUntil(&wakeTime, period);
    }
    int64_t nowUs = esp_timer_get_time();
    if (nowUs - lastRenderUs >= 100000)
    {
      renderScreen(nowUs);
      lastRenderUs = nowUs;
    }
  }
}
