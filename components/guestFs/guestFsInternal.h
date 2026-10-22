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
