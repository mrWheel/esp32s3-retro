#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum
{
  cpm86InfoDriveCount = 6,
  cpm86InfoCapacity = 4096,
  cpm86InfoStatusPort = 0xF8,
  cpm86InfoDataPort = 0xF9,
  cpm86InfoMagic = 0xA6,
  cpm86InfoSnapshot = 1
};
typedef struct
{
  bool mounted;
  bool readOnly;
  uint64_t bytes;
  const char* path;
} cpm86InfoDrive;
typedef struct
{
  const char* version;
  size_t guestRamBytes;
  size_t psramBytes;
  uint64_t uptimeMs;
  uint64_t instructions;
  cpm86InfoDrive drives[cpm86InfoDriveCount];
} cpm86InfoValues;
typedef struct
{
  char text[cpm86InfoCapacity];
  size_t length;
  size_t position;
} cpm86InfoStream;
void cpm86InfoBuild(cpm86InfoStream* stream, const cpm86InfoValues* values);
uint8_t cpm86InfoRead(cpm86InfoStream* stream);
