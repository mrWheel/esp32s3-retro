#pragma once
#include "apple2Core.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//-- Emulator information interface for the shared Apple II machine (UCSD Pascal and Apple II
// profiles).
//-- It mirrors the RETRO80/RETRO86 SYSINFO ports on slot-1 I/O space: a read of $C090 returns the
//-- magic when supported, a write of the snapshot value to $C090 builds a fresh snapshot, and
//-- reads of $C091 drain the byte stream (zero after the last byte). The stream starts with the
//-- RETROAPPLE-SYSINFO/1 signature so the guest can reject a foreign responder.

enum
{
  apple2InfoSlot = 1, //-- Slot 1 I/O space $C090-$C09F; slots 4-7 carry disks, slot 2 the timer.
  apple2InfoStatusRegister = 0x00, //-- $C090: magic on read, snapshot request on write.
  apple2InfoDataRegister = 0x01,   //-- $C091: next snapshot byte, zero after the last byte.
  apple2InfoCapacity = 4096,
  apple2InfoMagic = 0xA6,
  apple2InfoSnapshot = 1,
  apple2InfoVolumeCapacity = 16, //-- Slots 4..7 times units 1..4.
  apple2InfoVolumeNameCapacity = 16
};

typedef struct
{
  bool mounted;
  bool readOnly;
  bool smartPort;
  uint8_t slot;
  uint8_t unit;
  uint64_t bytes;
  const char* path;
  const char* volumeName; //-- Pascal/ProDOS volume label or NULL.
} apple2InfoVolume;

typedef struct
{
  const char* version;
  const char* profile;     //-- Machine profile line, e.g. "UCSD Pascal profile ...".
  const char* ramText;     //-- Guest RAM description line.
  const char* consoleText; //-- Console/display description line.
  size_t psramBytes;
  uint64_t uptimeMs;
  uint64_t cycles; //-- Emulated 6502 cycles since the machine started.
  apple2InfoVolume volumes[apple2InfoVolumeCapacity];
} apple2InfoValues;

typedef struct
{
  char text[apple2InfoCapacity];
  size_t length;
  size_t position;
} apple2InfoStream;

//-- Fills values from machine state when the guest arms a fresh snapshot.
typedef void (*apple2InfoCollect)(void* context, apple2InfoValues* values);

void apple2InfoBuild(apple2InfoStream* stream, const apple2InfoValues* values);
uint8_t apple2InfoRead(apple2InfoStream* stream);
//-- Attaches the interface to an unoccupied slot 1. The stream, collector and context must stay
// valid
//-- while the core runs (one Apple II machine instance exists at a time).
apple2CoreResult apple2InfoAttach(apple2Core* core, apple2InfoStream* stream,
                                  apple2InfoCollect collect, void* context);
