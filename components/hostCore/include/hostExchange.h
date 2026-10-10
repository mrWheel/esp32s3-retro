#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <dirent.h>
#include "hostCore.h"

enum
{
  hostExchangePort = 0xF8,
  hostExchangeAbortPort = 0xF9,
  hostExchangeCommandQuery = 0,
  hostExchangeCommandDirectory = 1,
  hostExchangeCommandGet = 2,
  hostExchangeCommandPut = 3,
  hostExchangeCommandPutOverwrite = 4,
  hostExchangeStatusOk = 0,
  hostExchangeStatusInvalid = 1,
  hostExchangeStatusUnavailable = 2,
  hostExchangeStatusExists = 3,
  hostExchangeStatusIoError = 4,
  hostExchangeStatusBusy = 5
};

typedef enum
{
  hostExchangeFilenameCpm80,
  hostExchangeFilenameProdos8
} hostExchangeFilenameFormat;

typedef enum
{
  hostExchangeIdle,
  hostExchangeStatus,
  hostExchangeCapabilities,
  hostExchangeDirectory,
  hostExchangeDirectoryFinalStatus,
  hostExchangeGetLength,
  hostExchangeGetData,
  hostExchangeGetChecksum,
  hostExchangeGetFinalStatus,
  hostExchangePutFilename,
  hostExchangePutLength,
  hostExchangePutChecksum,
  hostExchangePutStatus,
  hostExchangePutData,
  hostExchangePutFinalStatus
} hostExchangeState;

typedef struct
{
  char root[absolutePathCapacity];
  char filename[16];
  char finalPath[absolutePathCapacity];
  char temporaryPath[absolutePathCapacity];
  FILE *file;
  DIR *directory;
  hostExchangeState state;
  uint8_t activeCommand;
  uint8_t status;
  uint8_t nameBytes[16];
  uint8_t directoryName[16];
  size_t inputIndex;
  size_t outputIndex;
  uint32_t remaining;
  uint32_t crc;
  uint32_t expectedCrc;
  bool directoryNameReady;
  bool resumeDirectory;
  bool overwrite;
  hostExchangeFilenameFormat filenameFormat;
} hostExchange;

bool hostExchangeInitialize(hostExchange *exchange, const char *root);
bool hostExchangeInitializeProdos(hostExchange *exchange, const char *root);
void hostExchangeClose(hostExchange *exchange);
uint8_t hostExchangePortInput(void *context, uint8_t port);
void hostExchangePortOutput(void *context, uint8_t port, uint8_t value);
