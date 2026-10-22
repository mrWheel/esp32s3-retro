#pragma once
#include "guestFs.h"

enum
{
  guestFsJournalMaxRecords = 2048,
  guestFsJournalIdCapacity = 128
};

typedef struct
{
  FILE* file;
  char path[320];
  uint32_t records;
  uint32_t dataCrc;
  bool sealed;
} guestFsJournal;

extern const guestFsOps guestFsCpmOps;

uint32_t guestFsCrc32Update(uint32_t crc, const void* data, size_t length);

guestFsStatus guestFsJournalBegin(guestFsJournal* journal, const char* path, const char* imageId,
                                  uint64_t imageSize);
guestFsStatus guestFsJournalSave(guestFsJournal* journal, const guestFsIo* io, uint64_t offset,
                                 uint32_t length);
guestFsStatus guestFsJournalSeal(guestFsJournal* journal);
guestFsStatus guestFsJournalCommit(guestFsJournal* journal);
guestFsStatus guestFsJournalFinish(guestFsJournal* journal);
void guestFsJournalAbandon(guestFsJournal* journal);

#ifdef GUESTFS_FAULT_INJECTION
#define guestFsCrashPoint(stage) (guestFsFaultHook != NULL && guestFsFaultHook(stage))
#else
#define guestFsCrashPoint(stage) (false)
#endif

//-- Journaled metadata commit shared by the Apple II adapters (and usable by any block based file
//system).
//-- Every change is a byte range of the image (<= 512 bytes) whose old content is saved in the undo
//journal
//-- before the new content is written. Stage names for the fault hook: beforeSeal, sealed,
//slotWritten,
//-- applied, committed. Sets *needsReopen when the in-memory view can no longer be trusted.
typedef struct
{
  uint64_t offset;
  uint32_t length;
  const uint8_t* data;
} guestFsRangeChange;

guestFsStatus guestFsRangeCommit(const guestFsIo* io, const char* journalPath,
                                 const char* journalId, const guestFsRangeChange* changes,
                                 size_t count, bool* needsReopen);
bool guestFsJournalExists(const char* journalPath);

enum
{
  guestFsAppleBlockSize = 512,
  guestFsAppleSectorSize = 256,
  guestFsAppleDosImageSize = 143360
};

//-- An Apple II 16-sector image seen as 512-byte ProDOS blocks (and, for 140K, DOS 3.3
//track/sector).
//-- The sector order of the image file is chosen exactly like main/apple2DiskImage.c does for the
//emulator
//-- (extension .po / .do, else content probe, else DOS order), so the manager sees what the guest
//sees.
typedef struct
{
  guestFsIo io;
  uint32_t trackCount;
  bool dosOrder;
} guestFsAppleDevice;

//-- Returns guestFsErrUnsupported (detail filled) when the size is not 140K, 640K or 800K.
guestFsStatus guestFsAppleOpenDevice(guestFsAppleDevice* device, const guestFsIo* io,
                                     const char* imageId, char* detail, size_t detailCapacity);
uint32_t guestFsAppleBlockCount(const guestFsAppleDevice* device);
bool guestFsAppleReadBlock(const guestFsAppleDevice* device, uint32_t block, void* buffer);
bool guestFsAppleWriteBlock(const guestFsAppleDevice* device, uint32_t block, const void* buffer);
//-- Fills out[0..1] for one 512-byte block (1 range in ProDOS order, 2 ranges in DOS order);
//returns the count.
size_t guestFsAppleBlockChanges(const guestFsAppleDevice* device, uint32_t block,
                                const uint8_t* data, guestFsRangeChange out[2]);
//-- DOS 3.3 logical track/sector (256 bytes) of a 140K image.
bool guestFsAppleReadDosSector(const guestFsAppleDevice* device, uint32_t track, uint32_t sector,
                               void* buffer);
bool guestFsAppleWriteDosSector(const guestFsAppleDevice* device, uint32_t track, uint32_t sector,
                                const void* buffer);
void guestFsAppleDosSectorChange(const guestFsAppleDevice* device, uint32_t track, uint32_t sector,
                                 const uint8_t* data, guestFsRangeChange* out);

extern const guestFsOps guestFsProdosOps;
extern const guestFsOps guestFsPascalOps;
extern const guestFsOps guestFsDos33Ops;
bool guestFsAppleSupportsOs(const char* osId);
