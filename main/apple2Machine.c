#include "apple2Machine.h"
#include "apple2Core.h"
#include "hostConsole.h"
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
static apple2Core *guestCore;
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

static char decodeCharacter(uint8_t value)
{
  value &= 0x3F;
  if (value <= 0x1F)
  {
    return (char)(value + '@');
  }
  if (value == 0x20)
  {
    return ' ';
  }
  return (char)value;
}

static char decodeVidexCharacter(uint8_t value)
{
  value &= 0x7F;
  return value >= 0x20 ? (char)value : ' ';
}

static bool appendTextCell(char *buffer, size_t capacity, size_t *length, uint8_t value, bool flashOn,
                           bool *inverse, bool *flashing)
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
  char character = cellFlashing && !flashOn ? ' ' : decodeCharacter(value);
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
                            : appendTextCell(rowBuffer, sizeof(rowBuffer), &length, currentCells[column], flashOn,
                                             &inverse, &flashing);
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
    return coreResult == apple2CoreNoMemory ? ESP_ERR_NO_MEM : ESP_ERR_INVALID_STATE;
  }
  coreResult = apple2CoreLoadRom(guestCore, rom, apple2RomSize);
  free(rom);
  if (coreResult != apple2CoreOk)
  {
    apple2CoreDestroy(guestCore);
    guestCore = NULL;
    return ESP_ERR_INVALID_RESPONSE;
  }
  return ESP_OK;
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
  while (true)
  {
    handleInput(&skipLineFeed);
    if (apple2CoreRunCycles(guestCore, cyclesPerPeriod) != apple2CoreOk)
    {
      ESP_LOGE(tag, "6502 execution stopped");
      return;
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
