#define _POSIX_C_SOURCE 200809L
#include "hostExchange.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t updateCrc(uint32_t crc, uint8_t value)
{
  crc ^= value;
  for (uint8_t bit = 0; bit < 8; ++bit)
  {
    crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0);
  }
  return crc;
}

static bool decodeCpm80Name(const uint8_t name[11], char filename[13])
{
  size_t output = 0;
  bool inExtension = false;
  bool hasBase = false;
  bool hasExtension = false;
  bool baseEnded = false;
  bool extensionEnded = false;

  for (size_t index = 0; index < 11; ++index)
  {
    uint8_t character = name[index];
    bool extension = index >= 8;
    if (extension != inExtension)
    {
      inExtension = extension;
      if (hasBase && name[8] != ' ')
      {
        filename[output++] = '.';
      }
    }
    if (character == ' ')
    {
      if (extension)
      {
        extensionEnded = true;
      }
      else
      {
        baseEnded = true;
      }
      continue;
    }
    if ((baseEnded && !extension) || (extensionEnded && extension) ||
        !((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') ||
          character == '_' || character == '-'))
    {
      return false;
    }
    if (extension)
    {
      hasExtension = true;
    }
    else
    {
      hasBase = true;
    }
    filename[output++] = (char)character;
  }

  if (!hasBase || output == 0 || output >= 13)
  {
    return false;
  }
  if (hasExtension && filename[output - 1] == '.')
  {
    return false;
  }
  filename[output] = '\0';
  return true;
}

static bool encodeDirectoryName(const char *name, uint8_t encoded[13])
{
  uint8_t rawName[11];
  memset(rawName, ' ', sizeof(rawName));
  const char *extension = strchr(name, '.');
  size_t baseLength = extension == NULL ? strlen(name) : (size_t)(extension - name);
  size_t extensionLength = extension == NULL ? 0 : strlen(extension + 1);
  if (baseLength == 0 || baseLength > 8 || extensionLength > 3 ||
      (extension != NULL && extensionLength == 0))
  {
    return false;
  }
  for (size_t index = 0; index < baseLength; ++index)
  {
    unsigned char character = (unsigned char)name[index];
    if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == '_' || character == '-'))
    {
      return false;
    }
    rawName[index] = (uint8_t)(character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character);
  }
  for (size_t index = 0; index < extensionLength; ++index)
  {
    unsigned char character = (unsigned char)extension[index + 1];
    if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == '_' || character == '-'))
    {
      return false;
    }
    rawName[8 + index] =
        (uint8_t)(character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character);
  }
  size_t position = 0;
  for (size_t index = 0; index < 8; ++index)
  {
    if (rawName[index] != ' ')
    {
      encoded[position++] = rawName[index];
    }
  }
  if (extensionLength > 0)
  {
    encoded[position++] = '.';
    for (size_t index = 0; index < extensionLength; ++index)
    {
      encoded[position++] = rawName[8 + index];
    }
  }
  encoded[position] = '\0';
  return true;
}

static void closeTransfer(hostExchange *exchange, bool removeTemporary)
{
  if (exchange->file != NULL)
  {
    if (fclose(exchange->file) != 0)
    {
      exchange->status = hostExchangeStatusIoError;
    }
    exchange->file = NULL;
  }
  if (removeTemporary && exchange->temporaryPath[0] != '\0')
  {
    if (unlink(exchange->temporaryPath) != 0 && errno != ENOENT)
    {
      exchange->status = hostExchangeStatusIoError;
    }
  }
}

static void resetExchange(hostExchange *exchange, bool preserveDirectory)
{
  closeTransfer(exchange, true);
  if (!preserveDirectory && exchange->directory != NULL)
  {
    closedir(exchange->directory);
    exchange->directory = NULL;
  }
  exchange->resumeDirectory = false;
  exchange->state = hostExchangeIdle;
  exchange->activeCommand = 0;
  exchange->inputIndex = 0;
  exchange->outputIndex = 0;
  exchange->remaining = 0;
  exchange->crc = 0;
  exchange->expectedCrc = 0;
  memset(exchange->nameBytes, 0, sizeof(exchange->nameBytes));
  exchange->directoryNameReady = false;
  exchange->temporaryPath[0] = '\0';
}

static bool makePaths(hostExchange *exchange)
{
  int finalLength = snprintf(exchange->finalPath, sizeof(exchange->finalPath), "%s/%s",
                             exchange->root, exchange->filename);
  int temporaryLength = snprintf(exchange->temporaryPath, sizeof(exchange->temporaryPath),
                                 "%s/.HOSTCPM.PART", exchange->root);
  return finalLength >= 0 && (size_t)finalLength < sizeof(exchange->finalPath) && temporaryLength >= 0 &&
         (size_t)temporaryLength < sizeof(exchange->temporaryPath);
}

static void openReadFile(hostExchange *exchange)
{
  if (!decodeCpm80Name(exchange->nameBytes, exchange->filename))
  {
    exchange->status = hostExchangeStatusInvalid;
    exchange->state = hostExchangeStatus;
    return;
  }
  if (!makePaths(exchange))
  {
    exchange->status = hostExchangeStatusInvalid;
    exchange->state = hostExchangeStatus;
    return;
  }
  struct stat info;
  if (stat(exchange->finalPath, &info) != 0 || !S_ISREG(info.st_mode))
  {
    exchange->status = hostExchangeStatusUnavailable;
    exchange->state = hostExchangeStatus;
    return;
  }
  if (info.st_size < 0 || (uint64_t)info.st_size > UINT32_MAX)
  {
    exchange->status = hostExchangeStatusIoError;
    exchange->state = hostExchangeStatus;
    return;
  }
  exchange->file = fopen(exchange->finalPath, "rb");
  if (exchange->file == NULL)
  {
    exchange->status = hostExchangeStatusUnavailable;
    exchange->state = hostExchangeStatus;
    return;
  }
  exchange->remaining = (uint32_t)info.st_size;
  exchange->crc = 0xFFFFFFFFU;
  exchange->status = hostExchangeStatusOk;
  exchange->state = hostExchangeStatus;
}

static void prepareWriteFile(hostExchange *exchange)
{
  if (!decodeCpm80Name(exchange->nameBytes, exchange->filename))
  {
    exchange->status = hostExchangeStatusInvalid;
    exchange->state = hostExchangePutStatus;
    return;
  }
  if (!makePaths(exchange))
  {
    exchange->status = hostExchangeStatusInvalid;
    exchange->state = hostExchangePutStatus;
    return;
  }
  struct stat info;
  if (stat(exchange->finalPath, &info) == 0)
  {
    //-- PUT with the overwrite option only replaces a regular file; the old file stays until the new data is verified.
    if (!exchange->overwrite)
    {
      exchange->status = hostExchangeStatusExists;
      exchange->state = hostExchangeStatus;
      return;
    }
    if (!S_ISREG(info.st_mode))
    {
      exchange->status = hostExchangeStatusIoError;
      exchange->state = hostExchangeStatus;
      return;
    }
  }
  else if (errno != ENOENT)
  {
    exchange->status = hostExchangeStatusIoError;
    exchange->state = hostExchangeStatus;
    return;
  }
  int descriptor = open(exchange->temporaryPath, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (descriptor < 0)
  {
    exchange->status = errno == EEXIST   ? hostExchangeStatusBusy
                       : errno == ENOENT ? hostExchangeStatusUnavailable
                                         : hostExchangeStatusIoError;
    exchange->state = hostExchangeStatus;
    return;
  }
  exchange->file = fdopen(descriptor, "wb");
  if (exchange->file == NULL)
  {
    close(descriptor);
    unlink(exchange->temporaryPath);
    exchange->status = hostExchangeStatusIoError;
    exchange->state = hostExchangeStatus;
    return;
  }
  exchange->crc = 0xFFFFFFFFU;
  exchange->status = hostExchangeStatusOk;
  exchange->state = hostExchangeStatus;
}

static void beginCommand(hostExchange *exchange, uint8_t command)
{
  bool resumeDirectory = command == hostExchangeCommandGet &&
                         exchange->state == hostExchangeDirectory && exchange->directory != NULL;
  resetExchange(exchange, resumeDirectory);
  exchange->resumeDirectory = resumeDirectory;
  exchange->overwrite = command == hostExchangeCommandPutOverwrite;
  if (exchange->overwrite)
  {
    command = hostExchangeCommandPut;
  }
  exchange->activeCommand = command;
  exchange->status = hostExchangeStatusInvalid;
  switch (command)
  {
  case hostExchangeCommandQuery:
    exchange->status = hostExchangeStatusOk;
    exchange->state = hostExchangeStatus;
    break;
  case hostExchangeCommandDirectory:
    exchange->directory = opendir(exchange->root);
    exchange->status = exchange->directory == NULL ? hostExchangeStatusUnavailable : hostExchangeStatusOk;
    exchange->state = hostExchangeStatus;
    break;
  case hostExchangeCommandGet:
  case hostExchangeCommandPut:
    exchange->state = hostExchangePutFilename;
    break;
  default:
    exchange->status = hostExchangeStatusInvalid;
    exchange->state = hostExchangeStatus;
    break;
  }
  if (command == hostExchangeCommandGet)
  {
    exchange->state = hostExchangeGetLength;
    exchange->inputIndex = 0;
  }
  else if (command == hostExchangeCommandPut)
  {
    exchange->state = hostExchangePutFilename;
    exchange->inputIndex = 0;
  }
}

static void finishWrite(hostExchange *exchange)
{
  uint32_t actualCrc = exchange->crc ^ 0xFFFFFFFFU;
  bool valid = actualCrc == exchange->expectedCrc;
  if (exchange->file == NULL || !valid || fflush(exchange->file) != 0 || fsync(fileno(exchange->file)) != 0)
  {
    exchange->status = hostExchangeStatusIoError;
    closeTransfer(exchange, true);
    exchange->state = hostExchangePutFinalStatus;
    return;
  }
  if (fclose(exchange->file) != 0)
  {
    exchange->file = NULL;
    exchange->status = hostExchangeStatusIoError;
    closeTransfer(exchange, true);
    exchange->state = hostExchangePutFinalStatus;
    return;
  }
  exchange->file = NULL;
  struct stat info;
  if (stat(exchange->finalPath, &info) == 0)
  {
    //-- The overwrite option removes the old file only now, after the new data has been received and verified.
    if (!exchange->overwrite || !S_ISREG(info.st_mode) || unlink(exchange->finalPath) != 0)
    {
      exchange->status = exchange->overwrite ? hostExchangeStatusIoError : hostExchangeStatusExists;
      closeTransfer(exchange, true);
      exchange->state = hostExchangePutFinalStatus;
      return;
    }
  }
  else if (errno != ENOENT)
  {
    exchange->status = hostExchangeStatusIoError;
    closeTransfer(exchange, true);
    exchange->state = hostExchangePutFinalStatus;
    return;
  }
  if (rename(exchange->temporaryPath, exchange->finalPath) != 0)
  {
    exchange->status = errno == EEXIST ? hostExchangeStatusExists : hostExchangeStatusIoError;
    closeTransfer(exchange, true);
    exchange->state = hostExchangePutFinalStatus;
    return;
  }
  exchange->temporaryPath[0] = '\0';
  exchange->status = hostExchangeStatusOk;
  exchange->state = hostExchangePutFinalStatus;
}

static bool nextDirectoryName(hostExchange *exchange)
{
  exchange->directoryNameReady = false;
  exchange->outputIndex = 0;
  while (exchange->directory != NULL)
  {
    errno = 0;
    struct dirent *entry = readdir(exchange->directory);
    if (entry == NULL)
    {
      if (errno != 0)
      {
        exchange->status = hostExchangeStatusIoError;
      }
      break;
    }
    if (!encodeDirectoryName(entry->d_name, exchange->directoryName))
    {
      continue;
    }
    char path[absolutePathCapacity];
    int length = snprintf(path, sizeof(path), "%s/%s", exchange->root, entry->d_name);
    struct stat info;
    if (length < 0 || (size_t)length >= sizeof(path) || stat(path, &info) != 0 || !S_ISREG(info.st_mode))
    {
      continue;
    }
    exchange->directoryNameReady = true;
    return true;
  }
  if (exchange->directory != NULL)
  {
    closedir(exchange->directory);
    exchange->directory = NULL;
  }
  exchange->state = hostExchangeDirectoryFinalStatus;
  return false;
}

static bool isSafeRoot(const char *root)
{
  size_t length = strlen(root);
  if (length < 2 || root[0] != '/' || root[length - 1] == '/')
  {
    return false;
  }
  const char *segment = root + 1;
  for (const char *character = segment; ; ++character)
  {
    if (*character != '/' && *character != '\0')
    {
      continue;
    }
    size_t segmentLength = (size_t)(character - segment);
    if (segmentLength == 0 || (segmentLength == 1 && segment[0] == '.') ||
        (segmentLength == 2 && segment[0] == '.' && segment[1] == '.'))
    {
      return false;
    }
    if (*character == '\0')
    {
      return true;
    }
    segment = character + 1;
  }
}

bool hostExchangeInitialize(hostExchange *exchange, const char *root)
{
  if (exchange == NULL || root == NULL || !isSafeRoot(root) || strlen(root) >= sizeof(exchange->root))
  {
    return false;
  }
  memset(exchange, 0, sizeof(*exchange));
  memcpy(exchange->root, root, strlen(root) + 1);
  exchange->state = hostExchangeIdle;
  return true;
}

void hostExchangeClose(hostExchange *exchange)
{
  if (exchange == NULL)
  {
    return;
  }
  resetExchange(exchange, false);
  memset(exchange, 0, sizeof(*exchange));
}

uint8_t hostExchangePortInput(void *context, uint8_t port)
{
  hostExchange *exchange = context;
  if (exchange == NULL || port != hostExchangePort)
  {
    return 0xFF;
  }
  switch (exchange->state)
  {
  case hostExchangeStatus:
    if (exchange->status != hostExchangeStatusOk)
    {
      exchange->state = exchange->resumeDirectory ? hostExchangeDirectory : hostExchangeIdle;
      exchange->resumeDirectory = false;
    }
    else if (exchange->activeCommand == hostExchangeCommandDirectory)
    {
      exchange->state = hostExchangeDirectory;
    }
    else if (exchange->activeCommand == hostExchangeCommandQuery)
    {
      exchange->state = hostExchangeCapabilities;
      exchange->outputIndex = 0;
    }
    else if (exchange->activeCommand == hostExchangeCommandGet)
    {
      exchange->state = hostExchangeGetLength;
    }
    return exchange->status;
  case hostExchangeDirectory:
    if (!exchange->directoryNameReady && !nextDirectoryName(exchange))
    {
      return 0;
    }
    if (exchange->outputIndex >= sizeof(exchange->directoryName))
    {
      exchange->directoryNameReady = false;
      if (!nextDirectoryName(exchange))
      {
        return 0;
      }
    }
    {
      uint8_t value = exchange->directoryName[exchange->outputIndex++];
      if (value == 0)
      {
        exchange->directoryNameReady = false;
      }
      return value;
    }
  case hostExchangeDirectoryFinalStatus:
    exchange->state = hostExchangeIdle;
    return exchange->status;
  case hostExchangeCapabilities:
    if (exchange->outputIndex++ == 0)
    {
      return 1;
    }
    exchange->state = hostExchangeIdle;
    return 0x07;
  case hostExchangeGetLength:
  {
    uint8_t index = (uint8_t)exchange->outputIndex++;
    if (index < 4)
    {
      uint8_t value = (uint8_t)(exchange->remaining >> (index * 8));
      if (index == 3)
      {
        exchange->outputIndex = 0;
        exchange->state = exchange->remaining == 0 ? hostExchangeGetChecksum : hostExchangeGetData;
      }
      return value;
    }
    exchange->state = hostExchangeGetData;
    return 0;
  }
  case hostExchangeGetData:
  {
    int value = fgetc(exchange->file);
    if (value == EOF)
    {
      exchange->status = hostExchangeStatusIoError;
      value = 0;
    }
    exchange->crc = updateCrc(exchange->crc, (uint8_t)value);
    if (--exchange->remaining == 0)
    {
      exchange->state = hostExchangeGetChecksum;
      exchange->outputIndex = 0;
    }
    return (uint8_t)value;
  }
  case hostExchangeGetChecksum:
  {
    uint32_t checksum = exchange->crc ^ 0xFFFFFFFFU;
    uint8_t value = (uint8_t)(checksum >> (exchange->outputIndex * 8));
    if (++exchange->outputIndex == 4)
    {
      if (fclose(exchange->file) != 0)
      {
        exchange->status = hostExchangeStatusIoError;
      }
      exchange->file = NULL;
      exchange->state = hostExchangeGetFinalStatus;
    }
    return value;
  }
  case hostExchangeGetFinalStatus:
    exchange->state = exchange->resumeDirectory ? hostExchangeDirectory : hostExchangeIdle;
    exchange->resumeDirectory = false;
    return exchange->status;
  case hostExchangePutStatus:
    exchange->state = exchange->status == hostExchangeStatusOk ? hostExchangePutData : hostExchangeIdle;
    if (exchange->remaining == 0 && exchange->status == hostExchangeStatusOk)
    {
      finishWrite(exchange);
    }
    return exchange->status;
  case hostExchangePutFinalStatus:
    exchange->state = hostExchangeIdle;
    return exchange->status;
  default:
    return 0xFF;
  }
}

void hostExchangePortOutput(void *context, uint8_t port, uint8_t value)
{
  hostExchange *exchange = context;
  if (exchange == NULL)
  {
    return;
  }
  if (port == hostExchangeAbortPort)
  {
    bool resumeDirectory = value != 0 && exchange->directory != NULL &&
                           (exchange->resumeDirectory || exchange->state == hostExchangeDirectory);
    resetExchange(exchange, resumeDirectory);
    if (resumeDirectory)
    {
      exchange->state = hostExchangeDirectory;
    }
    return;
  }
  if (port != hostExchangePort)
  {
    return;
  }
  if (exchange->state == hostExchangeDirectory && value == hostExchangeCommandGet)
  {
    beginCommand(exchange, value);
    return;
  }
  switch (exchange->state)
  {
  case hostExchangeIdle:
    beginCommand(exchange, value);
    break;
  case hostExchangeGetLength:
  case hostExchangePutFilename:
    exchange->nameBytes[exchange->inputIndex++] = value;
    if (exchange->inputIndex == sizeof(exchange->nameBytes))
    {
      exchange->inputIndex = 0;
      if (exchange->state == hostExchangeGetLength)
      {
        openReadFile(exchange);
        if (exchange->state == hostExchangeStatus && exchange->status == hostExchangeStatusOk)
        {
          exchange->outputIndex = 0;
        }
      }
      else
      {
        exchange->state = hostExchangePutLength;
      }
    }
    break;
  case hostExchangePutLength:
    exchange->remaining |= (uint32_t)value << (exchange->inputIndex * 8);
    if (++exchange->inputIndex == 4)
    {
      exchange->inputIndex = 0;
      exchange->state = hostExchangePutChecksum;
    }
    break;
  case hostExchangePutChecksum:
    exchange->expectedCrc |= (uint32_t)value << (exchange->inputIndex * 8);
    if (++exchange->inputIndex == 4)
    {
      exchange->inputIndex = 0;
      uint32_t length = exchange->remaining;
      exchange->remaining = 0;
      prepareWriteFile(exchange);
      exchange->remaining = length;
      if (exchange->status == hostExchangeStatusOk)
      {
        exchange->state = hostExchangePutStatus;
      }
    }
    break;
  case hostExchangePutData:
    if (exchange->file == NULL || fputc(value, exchange->file) == EOF)
    {
      exchange->status = hostExchangeStatusIoError;
      closeTransfer(exchange, true);
      exchange->state = hostExchangePutFinalStatus;
    }
    else
    {
      exchange->crc = updateCrc(exchange->crc, value);
      if (--exchange->remaining == 0)
      {
        finishWrite(exchange);
      }
    }
    break;
  default:
    break;
  }
}
