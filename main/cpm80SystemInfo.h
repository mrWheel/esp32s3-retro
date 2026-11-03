#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
  cpm80InfoDriveCount = 6,
  cpm80InfoCapacity = 4096,
  cpm80InfoStatusPort = 0xF8,
  cpm80InfoDataPort = 0xF9,
  cpm80InfoMagic = 0xA6,
  cpm80InfoSnapshot = 1
};
typedef struct
{
  bool mounted;
  bool readOnly;
  uint64_t bytes;
  const char* path;
} cpm80InfoDrive;
typedef struct
{
  const char* version;
  size_t guestRamBytes;
  size_t psramBytes;
  uint64_t uptimeMs;
  uint64_t tstates;
  cpm80InfoDrive drives[cpm80InfoDriveCount];
} cpm80InfoValues;
typedef struct
{
  char text[cpm80InfoCapacity];
  size_t length;
  size_t position;
} cpm80InfoStream;
void cpm80InfoBuild(cpm80InfoStream* stream, const cpm80InfoValues* values);
uint8_t cpm80InfoRead(cpm80InfoStream* stream);
