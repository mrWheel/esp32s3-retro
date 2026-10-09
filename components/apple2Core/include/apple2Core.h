#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
  apple2RamSize = 48 * 1024,
  apple2RomSize = 12 * 1024,
  apple2TextRows = 24,
  apple2TextColumns = 40,
  apple2VidexTextRows = 24,
  apple2VidexTextColumns = 80
};

typedef struct apple2Core apple2Core;

typedef enum
{
  apple2CoreOk,
  apple2CoreInvalidArgument,
  apple2CoreNoMemory,
  apple2CoreInvalidRom,
  apple2CoreKeyBusy
} apple2CoreResult;

typedef struct
{
  bool textMode;
  bool mixedMode;
  bool page2;
  bool highResolution;
  bool videxTextMode;
} apple2VideoState;

//-- Reads one DOS-order 256-byte sector (track 0..34, logical sector 0..15) into buffer.
typedef bool (*apple2DiskReadSectorFunction)(void *context, uint8_t track, uint8_t sector, uint8_t *buffer);

typedef struct
{
  bool attached;
  bool motorOn;
  bool drive2Selected;
  bool q6;
  bool q7;
  uint8_t phases;
  uint8_t halfTrack;
  uint32_t sectorReads;
  uint32_t sectorReadFailures;
  uint32_t writeAttempts;
} apple2DiskState;

apple2CoreResult apple2CoreCreate(apple2Core **core);
void apple2CoreDestroy(apple2Core *core);
apple2CoreResult apple2CoreLoadRom(apple2Core *core, const uint8_t *rom, size_t size);
apple2CoreResult apple2CoreReset(apple2Core *core);
apple2CoreResult apple2CoreRunCycles(apple2Core *core, size_t cycles);
apple2CoreResult apple2CoreReadMemory(apple2Core *core, uint16_t address, uint8_t *value);
apple2CoreResult apple2CoreWriteMemory(apple2Core *core, uint16_t address, uint8_t value);
apple2CoreResult apple2CoreSetCharacterOptions(apple2Core *core, bool lowercaseCharacterRom,
                                               bool lowercaseKeyboard);
char apple2CoreDecodeTextCharacter(const apple2Core *core, uint8_t value);
apple2CoreResult apple2CorePressKey(apple2Core *core, uint8_t character);
bool apple2CoreKeyPending(const apple2Core *core);
void apple2CoreGetVideoState(const apple2Core *core, apple2VideoState *state);
bool apple2CoreTextAddress(bool page2, size_t row, size_t column, uint16_t *address);
apple2CoreResult apple2CoreReadTextCell(const apple2Core *core, bool page2, size_t row, size_t column,
                                        uint8_t *value);
apple2CoreResult apple2CoreReadVidexTextCell(const apple2Core *core, size_t row, size_t column,
                                             uint8_t *value);
bool apple2CoreGetVidexCursor(const apple2Core *core, size_t *row, size_t *column);
bool apple2CoreGetTextCursor(const apple2Core *core, size_t *row, size_t *column);
//-- Attach a read-only drive 1 (slot 6). The callback and context must stay valid until detach.
apple2CoreResult apple2CoreAttachDisk(apple2Core *core, apple2DiskReadSectorFunction readSector, void *context);
apple2CoreResult apple2CoreDetachDisk(apple2Core *core);
void apple2CoreGetDiskState(const apple2Core *core, apple2DiskState *state);
