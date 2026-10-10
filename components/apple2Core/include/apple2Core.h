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
  apple2VidexTextColumns = 80,
  apple2DiskFirstSlot = 4,
  apple2DiskLastSlot = 7,
  apple2DiskSlotCount = apple2DiskLastSlot - apple2DiskFirstSlot + 1,
  apple2DiskDrivesPerSlot = 2,
  apple2DiskMaxTracks = 160,
  apple2SmartPortSlot = 5,
  apple2SmartPortDevices = 2,
  apple2SmartPortBlockSize = 512
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

//-- Order in which the 16 sectors of a track are stored inside an image file.
//-- Dos: DOS 3.3 logical order (.do/.dsk). Prodos: ProDOS/Apple Pascal logical order (.po), where two
//-- logical sectors form one 512-byte block.
typedef enum
{
  apple2DiskSectorOrderDos,
  apple2DiskSectorOrderProdos
} apple2DiskSectorOrder;

//-- Reads one 256-byte sector (track 0..apple2DiskMaxTracks-1, image-order sector 0..15, see
//-- apple2DiskSectorOrder) into buffer.
typedef bool (*apple2DiskReadSectorFunction)(void *context, uint8_t track, uint8_t sector, uint8_t *buffer);
typedef bool (*apple2DiskWriteSectorFunction)(void *context, uint8_t track, uint8_t sector,
                                              const uint8_t *buffer);
typedef bool (*apple2SmartPortReadBlockFunction)(void *context, uint32_t block, uint8_t *buffer);
typedef bool (*apple2SmartPortWriteBlockFunction)(void *context, uint32_t block, const uint8_t *buffer);

typedef struct
{
  bool attached;
  bool motorOn;
  bool drive2Selected;
  bool q6;
  bool q7;
  uint8_t phases;
  uint16_t halfTrack;
  uint32_t sectorReads;
  uint32_t sectorReadFailures;
  uint32_t writeAttempts;
  uint32_t writeFailures;
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
//-- When enabled, every reset starts with the slot-3 80-column card selected as output (as if PR#3 was typed);
//-- the monitor SETVID routine then selects the card too. Call before apple2CoreLoadRom, which performs the reset.
apple2CoreResult apple2CoreSetBootIn80Columns(apple2Core *core, bool enabled);
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
//-- Attach a read-only 35-track DOS-order drive 1 (slot 6). The callback and context must stay valid until detach.
apple2CoreResult apple2CoreAttachDisk(apple2Core *core, apple2DiskReadSectorFunction readSector, void *context);
//-- Detaches every drive of every controller.
apple2CoreResult apple2CoreDetachDisk(apple2Core *core);
//-- State of the slot-6 controller (the selected drive's head position).
void apple2CoreGetDiskState(const apple2Core *core, apple2DiskState *state);
//-- Attach a read-only drive (slot apple2DiskFirstSlot..apple2DiskLastSlot, drive 0 or 1) with trackCount tracks
//-- (1..apple2DiskMaxTracks). A controller answers at its slot (I/O and boot ROM) as soon as one drive is attached.
apple2CoreResult apple2CoreAttachDiskDrive(apple2Core *core, uint8_t slot, uint8_t drive, apple2DiskSectorOrder order,
                                           uint8_t trackCount, apple2DiskReadSectorFunction readSector,
                                           void *context);
//-- The writable form enables writes only when a writeSector callback is provided.
apple2CoreResult apple2CoreAttachWritableDiskDrive(apple2Core *core, uint8_t slot, uint8_t drive,
                                                   apple2DiskSectorOrder order, uint8_t trackCount,
                                                   apple2DiskReadSectorFunction readSector,
                                                   apple2DiskWriteSectorFunction writeSector, void *context);
apple2CoreResult apple2CoreDetachDiskDrive(apple2Core *core, uint8_t slot, uint8_t drive);
//-- False when the slot is outside apple2DiskFirstSlot..apple2DiskLastSlot.
bool apple2CoreGetDiskStateForSlot(const apple2Core *core, uint8_t slot, apple2DiskState *state);
apple2CoreResult apple2CoreAttachSmartPortDevice(apple2Core *core, uint8_t unit, uint32_t blockCount,
                                                 apple2SmartPortReadBlockFunction readBlock,
                                                 apple2SmartPortWriteBlockFunction writeBlock, void *context);
apple2CoreResult apple2CoreDetachSmartPortDevice(apple2Core *core, uint8_t unit);
