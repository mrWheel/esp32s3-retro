#include "guestFs.h"
#include "guestFsInternal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#ifdef GUESTFS_FAULT_INJECTION
bool (*guestFsFaultHook)(const char* stage) = NULL;
#endif

//-- Adapters are tried in order; an adapter returns guestFsErrUnsupported when the image is not its
//kind.
static const guestFsOps* const adapters[] = {&guestFsCpmOps, &guestFsProdosOps, &guestFsPascalOps,
                                             &guestFsDos33Ops};
static const size_t adapterCount = sizeof(adapters) / sizeof(adapters[0]);

enum
{
  journalHeaderSize = 148,
  journalChunkSize = 256,
  journalMaxRecordLength = 65536,
  copyChunkSize = 4096
};

static const char journalMagic[8] = {'G', 'F', 'S', 'J', 'R', 'N', 'L', '1'};

const char* guestFsStatusText(guestFsStatus status)
{
  switch (status)
  {
  case guestFsOk:
    return "ok";
  case guestFsErrInvalidArgument:
    return "Invalid argument";
  case guestFsErrIo:
    return "I/O error on the disk image";
  case guestFsErrUnsupported:
    return "Unsupported disk image geometry or file system";
  case guestFsErrCorrupt:
    return "The disk image directory is damaged";
  case guestFsErrNotFound:
    return "File not found in the disk image";
  case guestFsErrExists:
    return "A file with that name already exists in the disk image";
  case guestFsErrNoSpace:
    return "Not enough free space in the disk image";
  case guestFsErrDirectoryFull:
    return "The disk image directory is full";
  case guestFsErrBadName:
    return "The file name is not valid for this disk image";
  case guestFsErrEmptyFile:
    return "Empty files cannot be stored in this disk image";
  case guestFsErrReadOnly:
    return "The disk image is read-only";
  case guestFsErrDamagedFile:
    return "The file has a damaged or sparse extent chain";
  case guestFsErrAborted:
    return "The operation was cancelled or aborted";
  }
  return "Unknown error";
}

//-- CRC-32 (IEEE, reflected); journals are small so a bitwise loop is enough.
uint32_t guestFsCrc32Update(uint32_t crc, const void* data, size_t length)
{
  const uint8_t* bytes = data;
  crc = ~crc;
  for (size_t index = 0; index < length; ++index)
  {
    crc ^= bytes[index];
    for (int bit = 0; bit < 8; ++bit)
    {
      crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
  }
  return ~crc;
}

static void putLe32(uint8_t* target, uint32_t value)
{
  target[0] = (uint8_t)value;
  target[1] = (uint8_t)(value >> 8);
  target[2] = (uint8_t)(value >> 16);
  target[3] = (uint8_t)(value >> 24);
}

static void putLe64(uint8_t* target, uint64_t value)
{
  putLe32(target, (uint32_t)value);
  putLe32(target + 4, (uint32_t)(value >> 32));
}

static uint32_t getLe32(const uint8_t* source)
{
  return (uint32_t)source[0] | ((uint32_t)source[1] << 8) | ((uint32_t)source[2] << 16) |
         ((uint32_t)source[3] << 24);
}

static uint64_t getLe64(const uint8_t* source)
{
  return (uint64_t)getLe32(source) | ((uint64_t)getLe32(source + 4) << 32);
}

static bool journalSync(FILE* file)
{
  return fflush(file) == 0 && fsync(fileno(file)) == 0;
}

static bool journalWrite(guestFsJournal* journal, const void* data, size_t length, bool countInCrc)
{
  if (fwrite(data, 1, length, journal->file) != length)
  {
    return false;
  }
  if (countInCrc)
  {
    journal->dataCrc = guestFsCrc32Update(journal->dataCrc, data, length);
  }
  return true;
}

guestFsStatus guestFsJournalBegin(guestFsJournal* journal, const char* path, const char* imageId,
                                  uint64_t imageSize)
{
  uint8_t header[journalHeaderSize];
  if (journal == NULL || path == NULL || imageId == NULL ||
      strlen(imageId) >= guestFsJournalIdCapacity || strlen(path) >= sizeof(journal->path))
  {
    return guestFsErrInvalidArgument;
  }
  memset(journal, 0, sizeof(*journal));
  strcpy(journal->path, path);
  memset(header, 0, sizeof(header));
  memcpy(header, journalMagic, sizeof(journalMagic));
  putLe64(header + 8, imageSize);
  memcpy(header + 16, imageId, strlen(imageId));
  putLe32(header + journalHeaderSize - 4, guestFsCrc32Update(0, header, journalHeaderSize - 4));
  journal->file = fopen(path, "wb");
  if (journal->file == NULL)
  {
    return guestFsErrIo;
  }
  if (!journalWrite(journal, header, sizeof(header), false))
  {
    guestFsJournalAbandon(journal);
    return guestFsErrIo;
  }
  return guestFsOk;
}

guestFsStatus guestFsJournalSave(guestFsJournal* journal, const guestFsIo* io, uint64_t offset,
                                 uint32_t length)
{
  uint8_t head[13];
  uint8_t chunk[journalChunkSize];
  if (journal == NULL || journal->file == NULL || journal->sealed || io == NULL || length == 0 ||
      length > journalMaxRecordLength || journal->records >= guestFsJournalMaxRecords)
  {
    return guestFsErrInvalidArgument;
  }
  head[0] = 'R';
  putLe64(head + 1, offset);
  putLe32(head + 9, length);
  if (!journalWrite(journal, head, sizeof(head), true))
  {
    return guestFsErrIo;
  }
  for (uint32_t done = 0; done < length;)
  {
    uint32_t count = length - done > sizeof(chunk) ? (uint32_t)sizeof(chunk) : length - done;
    if (!io->readAt(io->context, offset + done, chunk, count) ||
        !journalWrite(journal, chunk, count, true))
    {
      return guestFsErrIo;
    }
    done += count;
  }
  ++journal->records;
  return guestFsOk;
}

guestFsStatus guestFsJournalSeal(guestFsJournal* journal)
{
  uint8_t footer[9];
  if (journal == NULL || journal->file == NULL || journal->sealed)
  {
    return guestFsErrInvalidArgument;
  }
  if (guestFsCrashPoint("beforeSeal"))
  {
    return guestFsErrAborted;
  }
  if (!journalSync(journal->file))
  {
    return guestFsErrIo;
  }
  footer[0] = 'S';
  putLe32(footer + 1, journal->records);
  putLe32(footer + 5, journal->dataCrc);
  if (!journalWrite(journal, footer, sizeof(footer), false) || !journalSync(journal->file))
  {
    return guestFsErrIo;
  }
  journal->sealed = true;
  return guestFsOk;
}

guestFsStatus guestFsJournalCommit(guestFsJournal* journal)
{
  uint8_t footer[5];
  if (journal == NULL || journal->file == NULL || !journal->sealed)
  {
    return guestFsErrInvalidArgument;
  }
  footer[0] = 'D';
  putLe32(footer + 1, ~journal->dataCrc);
  if (!journalWrite(journal, footer, sizeof(footer), false) || !journalSync(journal->file))
  {
    return guestFsErrIo;
  }
  return guestFsOk;
}

guestFsStatus guestFsJournalFinish(guestFsJournal* journal)
{
  if (journal == NULL || journal->file == NULL)
  {
    return guestFsErrInvalidArgument;
  }
  bool closed = fclose(journal->file) == 0;
  journal->file = NULL;
  if (!closed || (remove(journal->path) != 0 && errno != ENOENT))
  {
    return guestFsErrIo;
  }
  return guestFsOk;
}

void guestFsJournalAbandon(guestFsJournal* journal)
{
  if (journal != NULL && journal->file != NULL)
  {
    fclose(journal->file);
    journal->file = NULL;
  }
}

typedef enum
{
  journalStateUnsealed,
  journalStateCommitted,
  journalStateRollback,
  journalStateCorrupt
} journalState;

static bool readHeader(FILE* file, uint8_t* header)
{
  return fread(header, 1, journalHeaderSize, file) == journalHeaderSize &&
         memcmp(header, journalMagic, sizeof(journalMagic)) == 0 &&
         getLe32(header + journalHeaderSize - 4) ==
             guestFsCrc32Update(0, header, journalHeaderSize - 4);
}

//-- Parses the journal; positions[] receives the file offset of each record tag.
static journalState scanJournal(FILE* file, long* positions, uint32_t* recordCount)
{
  uint8_t head[13];
  uint8_t chunk[journalChunkSize];
  uint32_t crc = 0;
  uint32_t count = 0;
  *recordCount = 0;
  for (;;)
  {
    long position = ftell(file);
    int tag = fgetc(file);
    if (tag == EOF)
    {
      return journalStateUnsealed;
    }
    if (tag == 'R')
    {
      head[0] = 'R';
      if (fread(head + 1, 1, sizeof(head) - 1, file) != sizeof(head) - 1)
      {
        return journalStateUnsealed;
      }
      uint32_t length = getLe32(head + 9);
      if (length == 0 || length > journalMaxRecordLength || count >= guestFsJournalMaxRecords)
      {
        return journalStateUnsealed;
      }
      crc = guestFsCrc32Update(crc, head, sizeof(head));
      for (uint32_t done = 0; done < length;)
      {
        uint32_t piece = length - done > sizeof(chunk) ? (uint32_t)sizeof(chunk) : length - done;
        if (fread(chunk, 1, piece, file) != piece)
        {
          return journalStateUnsealed;
        }
        crc = guestFsCrc32Update(crc, chunk, piece);
        done += piece;
      }
      positions[count++] = position;
      continue;
    }
    if (tag == 'S')
    {
      uint8_t footer[8];
      if (fread(footer, 1, sizeof(footer), file) != sizeof(footer))
      {
        return journalStateUnsealed;
      }
      if (getLe32(footer) != count || getLe32(footer + 4) != crc)
      {
        return journalStateCorrupt;
      }
      *recordCount = count;
      uint8_t done[5];
      if (fread(done, 1, sizeof(done), file) == sizeof(done) && done[0] == 'D' &&
          getLe32(done + 1) == ~crc)
      {
        return journalStateCommitted;
      }
      return journalStateRollback;
    }
    return journalStateUnsealed;
  }
}

guestFsStatus guestFsJournalPeek(const char* journalPath, char* imageId, size_t imageIdCapacity)
{
  uint8_t header[journalHeaderSize];
  if (journalPath == NULL || imageId == NULL || imageIdCapacity == 0)
  {
    return guestFsErrInvalidArgument;
  }
  FILE* file = fopen(journalPath, "rb");
  if (file == NULL)
  {
    return errno == ENOENT ? guestFsErrNotFound : guestFsErrIo;
  }
  bool valid = readHeader(file, header);
  fclose(file);
  if (!valid)
  {
    return guestFsErrCorrupt;
  }
  char id[guestFsJournalIdCapacity];
  memcpy(id, header + 16, sizeof(id) - 1);
  id[sizeof(id) - 1] = '\0';
  if (strlen(id) >= imageIdCapacity)
  {
    return guestFsErrInvalidArgument;
  }
  strcpy(imageId, id);
  return guestFsOk;
}

static guestFsStatus rollbackRecords(FILE* file, const guestFsIo* io, const long* positions,
                                     uint32_t recordCount)
{
  uint8_t head[13];
  uint8_t chunk[journalChunkSize];
  for (uint32_t index = recordCount; index > 0; --index)
  {
    if (fseek(file, positions[index - 1], SEEK_SET) != 0 ||
        fread(head, 1, sizeof(head), file) != sizeof(head))
    {
      return guestFsErrIo;
    }
    uint64_t offset = getLe64(head + 1);
    uint32_t length = getLe32(head + 9);
    for (uint32_t done = 0; done < length;)
    {
      uint32_t piece = length - done > sizeof(chunk) ? (uint32_t)sizeof(chunk) : length - done;
      if (fread(chunk, 1, piece, file) != piece ||
          !io->writeAt(io->context, offset + done, chunk, piece))
      {
        return guestFsErrIo;
      }
      done += piece;
    }
  }
  return io->flush(io->context) ? guestFsOk : guestFsErrIo;
}

guestFsStatus guestFsJournalRecover(const char* journalPath, const guestFsIo* io)
{
  uint8_t header[journalHeaderSize];
  if (journalPath == NULL)
  {
    return guestFsErrInvalidArgument;
  }
  FILE* file = fopen(journalPath, "rb");
  if (file == NULL)
  {
    return errno == ENOENT ? guestFsOk : guestFsErrIo;
  }
  journalState state = journalStateUnsealed;
  uint32_t recordCount = 0;
  long* positions = NULL;
  guestFsStatus status = guestFsOk;
  if (readHeader(file, header))
  {
    positions = malloc(sizeof(long) * guestFsJournalMaxRecords);
    if (positions == NULL)
    {
      fclose(file);
      return guestFsErrIo;
    }
    state = scanJournal(file, positions, &recordCount);
    if (state == journalStateRollback)
    {
      if (io == NULL)
      {
        status = guestFsErrInvalidArgument;
      }
      else if (io->readOnly)
      {
        status = guestFsErrReadOnly;
      }
      else if (getLe64(header + 8) != io->size)
      {
        status = guestFsErrCorrupt;
      }
      else
      {
        status = rollbackRecords(file, io, positions, recordCount);
      }
    }
    free(positions);
  }
  fclose(file);
  if (state == journalStateCorrupt)
  {
    return guestFsErrCorrupt;
  }
  if (status != guestFsOk)
  {
    return status;
  }
  return remove(journalPath) == 0 || errno == ENOENT ? guestFsOk : guestFsErrIo;
}

bool guestFsSupportsOs(const char* osId)
{
  for (size_t index = 0; osId != NULL && index < adapterCount; ++index)
  {
    if (adapters[index]->supportsOs(osId))
    {
      return true;
    }
  }
  return false;
}

guestFsStatus guestFsOpen(const guestFsIo* io, const char* osId, const char* journalPath,
                          const char* journalId, guestFsVolume** volume, char* detail,
                          size_t detailCapacity)
{
  if (io == NULL || osId == NULL || journalPath == NULL || journalId == NULL || volume == NULL ||
      io->readAt == NULL || io->writeAt == NULL || io->flush == NULL)
  {
    return guestFsErrInvalidArgument;
  }
  if (detail != NULL && detailCapacity > 0)
  {
    detail[0] = '\0';
  }
  *volume = NULL;
  guestFsStatus last = guestFsErrUnsupported;
  for (size_t index = 0; index < adapterCount; ++index)
  {
    if (!adapters[index]->supportsOs(osId))
    {
      continue;
    }
    last = adapters[index]->open(io, osId, journalPath, journalId, volume, detail, detailCapacity);
    if (last != guestFsErrUnsupported)
    {
      return last;
    }
  }
  return last;
}

void guestFsClose(guestFsVolume* volume)
{
  if (volume != NULL)
  {
    volume->ops->close(volume);
  }
}

void guestFsInfoGet(const guestFsVolume* volume, guestFsInfo* info)
{
  volume->ops->info(volume, info);
}

guestFsStatus guestFsList(guestFsVolume* volume, guestFsListFn callback, void* context)
{
  return volume == NULL || callback == NULL ? guestFsErrInvalidArgument
                                            : volume->ops->list(volume, callback, context);
}

guestFsStatus guestFsStat(guestFsVolume* volume, uint8_t user, const char* name,
                          guestFsEntry* entry)
{
  return volume == NULL || name == NULL || entry == NULL
             ? guestFsErrInvalidArgument
             : volume->ops->stat(volume, user, name, entry);
}

guestFsStatus guestFsReadAt(guestFsVolume* volume, uint8_t user, const char* name, uint32_t offset,
                            void* buffer, size_t length, size_t* got)
{
  return volume == NULL || name == NULL || buffer == NULL || got == NULL
             ? guestFsErrInvalidArgument
             : volume->ops->readAt(volume, user, name, offset, buffer, length, got);
}

guestFsStatus guestFsWriteEx(guestFsVolume* volume, uint8_t user, const char* name, uint32_t length,
                             guestFsSourceFn source, void* context, bool overwrite,
                             const guestFsAttributes* attributes)
{
  return volume == NULL || name == NULL || source == NULL
             ? guestFsErrInvalidArgument
             : volume->ops->write(volume, user, name, length, source, context, overwrite,
                                  attributes);
}

guestFsStatus guestFsWrite(guestFsVolume* volume, uint8_t user, const char* name, uint32_t length,
                           guestFsSourceFn source, void* context, bool overwrite)
{
  return guestFsWriteEx(volume, user, name, length, source, context, overwrite, NULL);
}

guestFsStatus guestFsRemove(guestFsVolume* volume, uint8_t user, const char* name)
{
  return volume == NULL || name == NULL ? guestFsErrInvalidArgument
                                        : volume->ops->remove(volume, user, name);
}

guestFsStatus guestFsRename(guestFsVolume* volume, uint8_t user, const char* name,
                            const char* newName)
{
  return volume == NULL || name == NULL || newName == NULL
             ? guestFsErrInvalidArgument
             : volume->ops->rename(volume, user, name, newName);
}

typedef struct
{
  guestFsVolume* volume;
  uint8_t user;
  const char* name;
  uint32_t offset;
  guestFsPollFn poll;
  void* pollContext;
  guestFsStatus status;
} copySource;

static bool copySourceRead(void* context, void* buffer, size_t length)
{
  copySource* source = context;
  uint8_t* target = buffer;
  while (length > 0)
  {
    if (source->poll != NULL && source->poll(source->pollContext))
    {
      source->status = guestFsErrAborted;
      return false;
    }
    size_t got = 0;
    source->status = guestFsReadAt(source->volume, source->user, source->name, source->offset,
                                   target, length, &got);
    if (source->status != guestFsOk)
    {
      return false;
    }
    if (got == 0)
    {
      source->status = guestFsErrIo;
      return false;
    }
    target += got;
    length -= got;
    source->offset += (uint32_t)got;
  }
  return true;
}

guestFsStatus guestFsCopy(guestFsVolume* source, uint8_t sourceUser, const char* sourceName,
                          guestFsVolume* destination, uint8_t destinationUser,
                          const char* destinationName, bool overwrite, guestFsPollFn poll,
                          void* pollContext)
{
  guestFsEntry entry;
  guestFsStatus status = guestFsStat(source, sourceUser, sourceName, &entry);
  if (status != guestFsOk)
  {
    return status;
  }
  if (entry.damaged)
  {
    return guestFsErrDamagedFile;
  }
  if (entry.size == 0)
  {
    return guestFsErrEmptyFile;
  }
  if (source == destination && sourceUser == destinationUser && destinationName != NULL &&
      strcasecmp(sourceName, destinationName) == 0)
  {
    return guestFsErrInvalidArgument;
  }
  copySource reader = {source, sourceUser, sourceName, 0, poll, pollContext, guestFsOk};
  status = guestFsWriteEx(destination, destinationUser, destinationName, entry.size, copySourceRead,
                          &reader, overwrite, entry.hasAttributes ? &entry.attributes : NULL);
  if (status != guestFsOk && reader.status != guestFsOk)
  {
    return reader.status;
  }
  return status;
}

guestFsStatus guestFsExportToFile(guestFsVolume* volume, uint8_t user, const char* name,
                                  FILE* output, guestFsPollFn poll, void* pollContext)
{
  guestFsEntry entry;
  if (output == NULL)
  {
    return guestFsErrInvalidArgument;
  }
  guestFsStatus status = guestFsStat(volume, user, name, &entry);
  if (status != guestFsOk)
  {
    return status;
  }
  if (entry.damaged)
  {
    return guestFsErrDamagedFile;
  }
  uint8_t* buffer = malloc(copyChunkSize);
  if (buffer == NULL)
  {
    return guestFsErrIo;
  }
  uint32_t offset = 0;
  while (offset < entry.size && status == guestFsOk)
  {
    if (poll != NULL && poll(pollContext))
    {
      status = guestFsErrAborted;
      break;
    }
    size_t wanted = entry.size - offset > copyChunkSize ? copyChunkSize : entry.size - offset;
    size_t got = 0;
    status = guestFsReadAt(volume, user, name, offset, buffer, wanted, &got);
    if (status == guestFsOk && (got == 0 || fwrite(buffer, 1, got, output) != got))
    {
      status = guestFsErrIo;
    }
    offset += (uint32_t)got;
  }
  free(buffer);
  return status;
}

typedef struct
{
  FILE* input;
  guestFsPollFn poll;
  void* pollContext;
  bool cancelled;
  bool failed;
} fileSource;

static bool fileSourceRead(void* context, void* buffer, size_t length)
{
  fileSource* source = context;
  if (source->poll != NULL && source->poll(source->pollContext))
  {
    source->cancelled = true;
    return false;
  }
  if (fread(buffer, 1, length, source->input) != length)
  {
    source->failed = true;
    return false;
  }
  return true;
}

guestFsStatus guestFsImportFromFile(guestFsVolume* volume, uint8_t user, const char* name,
                                    FILE* input, uint32_t length, bool overwrite,
                                    guestFsPollFn poll, void* pollContext)
{
  if (input == NULL)
  {
    return guestFsErrInvalidArgument;
  }
  fileSource source = {input, poll, pollContext, false, false};
  guestFsStatus status =
      guestFsWrite(volume, user, name, length, fileSourceRead, &source, overwrite);
  if (status != guestFsOk && source.cancelled)
  {
    return guestFsErrAborted;
  }
  if (status != guestFsOk && source.failed)
  {
    return guestFsErrIo;
  }
  return status;
}
