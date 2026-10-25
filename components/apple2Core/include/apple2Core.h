#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hostClock.h"

enum
{
  apple2RamSize = 48 * 1024,
  apple2RomSize = 12 * 1024,
  apple2IIeRomSize = 16 * 1024,
  apple2TextRows = 24,
  apple2TextColumns = 40,
  apple2VidexTextRows = 24,
  apple2VidexTextColumns = 80,
  apple2DiskFirstSlot = 4,
  apple2DiskLastSlot = 7,
  apple2DiskSlotCount = apple2DiskLastSlot - apple2DiskFirstSlot + 1,
  apple2DiskDrivesPerSlot = 2,
  apple2DiskMaxTracks = 200,
  apple2SmartPortSlot = 5,
  apple2SmartPortDevices = 4,
  apple2SmartPortBlockSize = 512,
  apple2TimerSlot = 2,
  apple2TimerCapabilityAddress = 0xC0A0,
  apple2TimerLowAddress = 0xC0A1,
  apple2TimerHighAddress = 0xC0A4,
  apple2TimerCapability = 0xC1
};

typedef struct apple2Core apple2Core;

typedef uint8_t (*apple2SlotIoReadFunction)(void* context, uint8_t registerIndex);
typedef void (*apple2SlotIoWriteFunction)(void* context, uint8_t registerIndex, uint8_t value);

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
  bool store80;
  bool col80;
  bool altCharSet;
} apple2VideoState;

//-- Order in which the 16 sectors of a track are stored inside an image file.
//-- Dos: DOS 3.3 logical order (.do/.dsk). Prodos: ProDOS/Apple Pascal logical order (.po), where
//two
//-- logical sectors form one 512-byte block.
typedef enum
{
  apple2DiskSectorOrderDos,
  apple2DiskSectorOrderProdos
} apple2DiskSectorOrder;

//-- Reads one 256-byte sector (track 0..apple2DiskMaxTracks-1, image-order sector 0..15, see
//-- apple2DiskSectorOrder) into buffer.
typedef bool (*apple2DiskReadSectorFunction)(void* context, uint8_t track, uint8_t sector,
                                             uint8_t* buffer);
typedef bool (*apple2DiskWriteSectorFunction)(void* context, uint8_t track, uint8_t sector,
                                              const uint8_t* buffer);
typedef bool (*apple2SmartPortReadBlockFunction)(void* context, uint32_t block, uint8_t* buffer);
typedef bool (*apple2SmartPortWriteBlockFunction)(void* context, uint32_t block,
                                                  const uint8_t* buffer);

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

typedef enum
{
  apple2RamCardLanguage16 = 0,
  apple2RamCardSaturn128
} apple2RamCard;

apple2CoreResult apple2CoreCreate(apple2Core** core);
void apple2CoreDestroy(apple2Core* core);
//-- Configures the APPLE2_TIMER_C1 virtual clock in slot 2. $C0A0 reports C1h when a callback is
//-- present, otherwise 00h. $C0A1 samples one unsigned millisecond value and returns bits 0-7;
//-- $C0A2-$C0A4 return the remaining little-endian bytes and are zero before the first sample or
//-- after reset. With no callback, capability and time reads return zero. Reset clears only the
//-- snapshot. Writes are ignored. The callback/context are caller-owned and must remain valid while
//-- configured. This reserves slot 2 from slot-I/O devices.
apple2CoreResult apple2CoreSetTimeSource(apple2Core* core, hostReadMilliseconds readMilliseconds,
                                         void* context);
//-- Selects the slot-0 RAM card: the 16 KiB Language Card (default) or a Saturn 128K card (eight 16
//KiB banks).
//-- Clears the card RAM; call before apple2CoreLoadRom, which performs the reset.
apple2CoreResult apple2CoreSetRamCard(apple2Core* core, apple2RamCard ramCard);
//-- Turns the machine into an Apple IIe (64 KiB main + 64 KiB auxiliary memory, built-in Language
//Card, 80-column
//-- softswitches). Call before apple2CoreLoadRom, which then expects the 16 KiB IIe ROM
//($C000-$FFFF, NMOS-compatible
//-- unenhanced version). The slot-0 RAM card, the Videx card and the Pascal card emulation are not
//used in this mode.
apple2CoreResult apple2CoreEnableIIe(apple2Core* core);
bool apple2CoreIsIIe(const apple2Core* core);
apple2CoreResult apple2CoreLoadRom(apple2Core* core, const uint8_t* rom, size_t size);
apple2CoreResult apple2CoreReset(apple2Core* core);
apple2CoreResult apple2CoreRunCycles(apple2Core* core, size_t cycles);
apple2CoreResult apple2CoreReadMemory(apple2Core* core, uint16_t address, uint8_t* value);
apple2CoreResult apple2CoreWriteMemory(apple2Core* core, uint16_t address, uint8_t value);
apple2CoreResult apple2CoreSetCharacterOptions(apple2Core* core, bool lowercaseCharacterRom,
                                               bool lowercaseKeyboard);
//-- When enabled, every reset starts with the slot-3 80-column card selected as output (as if PR#3
//was typed);
//-- the monitor SETVID routine then selects the card too. Call before apple2CoreLoadRom, which
//performs the reset.
apple2CoreResult apple2CoreSetBootIn80Columns(apple2Core* core, bool enabled);
char apple2CoreDecodeTextCharacter(const apple2Core* core, uint8_t value);
apple2CoreResult apple2CorePressKey(apple2Core* core, uint8_t character);
bool apple2CoreKeyPending(const apple2Core* core);
void apple2CoreGetVideoState(const apple2Core* core, apple2VideoState* state);
bool apple2CoreTextAddress(bool page2, size_t row, size_t column, uint16_t* address);
apple2CoreResult apple2CoreReadTextCell(const apple2Core* core, bool page2, size_t row,
                                        size_t column, uint8_t* value);
apple2CoreResult apple2CoreReadVidexTextCell(const apple2Core* core, size_t row, size_t column,
                                             uint8_t* value);
bool apple2CoreGetVidexCursor(const apple2Core* core, size_t* row, size_t* column);
bool apple2CoreGetTextCursor(const apple2Core* core, size_t* row, size_t* column);
//-- Attach a read-only 35-track DOS-order drive 1 (slot 6). The callback and context must stay
//valid until detach.
apple2CoreResult apple2CoreAttachDisk(apple2Core* core, apple2DiskReadSectorFunction readSector,
                                      void* context);
//-- Detaches every drive of every controller.
apple2CoreResult apple2CoreDetachDisk(apple2Core* core);
//-- State of the slot-6 controller (the selected drive's head position).
void apple2CoreGetDiskState(const apple2Core* core, apple2DiskState* state);
//-- Attach a read-only drive (slot apple2DiskFirstSlot..apple2DiskLastSlot, drive 0 or 1) with
//trackCount tracks
//-- (1..apple2DiskMaxTracks). A controller answers at its slot (I/O and boot ROM) as soon as one
//drive is attached.
apple2CoreResult apple2CoreAttachDiskDrive(apple2Core* core, uint8_t slot, uint8_t drive,
                                           apple2DiskSectorOrder order, uint8_t trackCount,
                                           apple2DiskReadSectorFunction readSector, void* context);
//-- The writable form enables writes only when a writeSector callback is provided.
apple2CoreResult apple2CoreAttachWritableDiskDrive(apple2Core* core, uint8_t slot, uint8_t drive,
                                                   apple2DiskSectorOrder order, uint8_t trackCount,
                                                   apple2DiskReadSectorFunction readSector,
                                                   apple2DiskWriteSectorFunction writeSector,
                                                   void* context);
apple2CoreResult apple2CoreDetachDiskDrive(apple2Core* core, uint8_t slot, uint8_t drive);
//-- False when the slot is outside apple2DiskFirstSlot..apple2DiskLastSlot.
bool apple2CoreGetDiskStateForSlot(const apple2Core* core, uint8_t slot, apple2DiskState* state);
//-- Attach a 512-byte-block device (unit 1..apple2SmartPortDevices) to slot
//apple2DiskFirstSlot..apple2DiskLastSlot.
//-- A slot hosts either Disk II drives or block devices, never both. With autostart the slot also
//carries the boot
//-- signature, so the Autostart ROM boots unit 1 at reset (ProDOS 8 boot volume); without it the
//slot is a plain
//-- SmartPort interface.
apple2CoreResult apple2CoreAttachBlockDevice(apple2Core* core, uint8_t slot, uint8_t unit,
                                             uint32_t blockCount, bool autostart,
                                             apple2SmartPortReadBlockFunction readBlock,
                                             apple2SmartPortWriteBlockFunction writeBlock,
                                             void* context);
apple2CoreResult apple2CoreDetachBlockDevice(apple2Core* core, uint8_t slot, uint8_t unit);
//-- Slot-5 forms of the calls above (units 1..apple2SmartPortDevices, no autostart).
apple2CoreResult apple2CoreAttachSmartPortDevice(apple2Core* core, uint8_t unit,
                                                 uint32_t blockCount,
                                                 apple2SmartPortReadBlockFunction readBlock,
                                                 apple2SmartPortWriteBlockFunction writeBlock,
                                                 void* context);
apple2CoreResult apple2CoreDetachSmartPortDevice(apple2Core* core, uint8_t unit);
//-- Slot I/O callbacks are supported on unoccupied slots 1 and 2; configuring the timer reserves
//slot 2.
apple2CoreResult apple2CoreAttachSlotIoDevice(apple2Core* core, uint8_t slot,
                                              apple2SlotIoReadFunction readRegister,
                                              apple2SlotIoWriteFunction writeRegister,
                                              void* context);
apple2CoreResult apple2CoreDetachSlotIoDevice(apple2Core* core, uint8_t slot);
