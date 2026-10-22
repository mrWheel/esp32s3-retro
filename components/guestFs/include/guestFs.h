#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum
{
  guestFsNameCapacity = 24,
  guestFsDetailCapacity = 96,
  guestFsMaxUser = 15
};

typedef enum
{
  guestFsOk = 0,
  guestFsErrInvalidArgument,
  guestFsErrIo,
  guestFsErrUnsupported,
  guestFsErrCorrupt,
  guestFsErrNotFound,
  guestFsErrExists,
  guestFsErrNoSpace,
  guestFsErrDirectoryFull,
  guestFsErrBadName,
  guestFsErrEmptyFile,
  guestFsErrReadOnly,
  guestFsErrDamagedFile,
  guestFsErrAborted
} guestFsStatus;

const char* guestFsStatusText(guestFsStatus status);

//-- Block I/O supplied by the caller (main/guestFsImage.c binds it to imageFile).
typedef struct
{
  void* context;
  bool (*readAt)(void* context, uint64_t offset, void* buffer, size_t length);
  bool (*writeAt)(void* context, uint64_t offset, const void* buffer, size_t length);
  bool (*flush)(void* context);
  uint64_t size;
  bool readOnly;
} guestFsIo;

typedef struct
{
  char name[guestFsNameCapacity];
  uint8_t user;
  uint32_t size;
  uint32_t allocatedBytes;
  bool readOnlyAttribute;
  bool systemAttribute;
  bool damaged;
} guestFsEntry;

typedef struct
{
  const char* fsName;
  const char* profileName;
  uint32_t blockSize;
  uint32_t totalBlocks;
  uint32_t freeBlocks;
  uint32_t directoryEntries;
  uint32_t freeDirectoryEntries;
  uint8_t maxUser;
} guestFsInfo;

typedef struct guestFsVolume guestFsVolume;

//-- Return false to stop the listing.
typedef bool (*guestFsListFn)(void* context, const guestFsEntry* entry);
//-- Deliver exactly length bytes into buffer; return false to abort the write.
typedef bool (*guestFsSourceFn)(void* context, void* buffer, size_t length);
//-- Called between chunks; return true to cancel the operation.
typedef bool (*guestFsPollFn)(void* context);

typedef struct guestFsOps
{
  const char* fsName;
  bool (*supportsOs)(const char* osId);
  guestFsStatus (*open)(const guestFsIo* io, const char* osId, const char* journalPath,
                        const char* journalId, guestFsVolume** volume, char* detail,
                        size_t detailCapacity);
  void (*close)(guestFsVolume* volume);
  void (*info)(const guestFsVolume* volume, guestFsInfo* info);
  guestFsStatus (*list)(guestFsVolume* volume, guestFsListFn callback, void* context);
  guestFsStatus (*stat)(guestFsVolume* volume, uint8_t user, const char* name, guestFsEntry* entry);
  guestFsStatus (*readAt)(guestFsVolume* volume, uint8_t user, const char* name, uint32_t offset,
                          void* buffer, size_t length, size_t* got);
  guestFsStatus (*write)(guestFsVolume* volume, uint8_t user, const char* name, uint32_t length,
                         guestFsSourceFn source, void* context, bool overwrite);
  guestFsStatus (*remove)(guestFsVolume* volume, uint8_t user, const char* name);
  guestFsStatus (*rename)(guestFsVolume* volume, uint8_t user, const char* name,
                          const char* newName);
} guestFsOps;

struct guestFsVolume
{
  const guestFsOps* ops;
};

bool guestFsSupportsOs(const char* osId);
guestFsStatus guestFsOpen(const guestFsIo* io, const char* osId, const char* journalPath,
                          const char* journalId, guestFsVolume** volume, char* detail,
                          size_t detailCapacity);
void guestFsClose(guestFsVolume* volume);
void guestFsInfoGet(const guestFsVolume* volume, guestFsInfo* info);
guestFsStatus guestFsList(guestFsVolume* volume, guestFsListFn callback, void* context);
guestFsStatus guestFsStat(guestFsVolume* volume, uint8_t user, const char* name,
                          guestFsEntry* entry);
guestFsStatus guestFsReadAt(guestFsVolume* volume, uint8_t user, const char* name, uint32_t offset,
                            void* buffer, size_t length, size_t* got);
guestFsStatus guestFsWrite(guestFsVolume* volume, uint8_t user, const char* name, uint32_t length,
                           guestFsSourceFn source, void* context, bool overwrite);
guestFsStatus guestFsRemove(guestFsVolume* volume, uint8_t user, const char* name);
guestFsStatus guestFsRename(guestFsVolume* volume, uint8_t user, const char* name,
                            const char* newName);

//-- Streaming helpers (no whole-file buffers).
guestFsStatus guestFsCopy(guestFsVolume* source, uint8_t sourceUser, const char* sourceName,
                          guestFsVolume* destination, uint8_t destinationUser,
                          const char* destinationName, bool overwrite, guestFsPollFn poll,
                          void* pollContext);
guestFsStatus guestFsExportToFile(guestFsVolume* volume, uint8_t user, const char* name,
                                  FILE* output, guestFsPollFn poll, void* pollContext);
guestFsStatus guestFsImportFromFile(guestFsVolume* volume, uint8_t user, const char* name,
                                    FILE* input, uint32_t length, bool overwrite,
                                    guestFsPollFn poll, void* pollContext);

//-- Crash recovery. Returns guestFsOk when no journal exists or it was resolved.
guestFsStatus guestFsJournalPeek(const char* journalPath, char* imageId, size_t imageIdCapacity);
guestFsStatus guestFsJournalRecover(const char* journalPath, const guestFsIo* io);

#ifdef GUESTFS_FAULT_INJECTION
//-- Test hook: return true to simulate power loss at the named stage.
extern bool (*guestFsFaultHook)(const char* stage);
#endif
