#include "hostCore.h"
#include "hostExchange.h"
#include "imageFile.h"
#include "cpm80Cpu.h"
#include "cpm80Guest.h"
#include "cpm80DriveConfig.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static void testPaths(void)
{
  const char *rejected[] = {"../images/cpm80/disk",
                            "%2e%2e/images",
                            "cpm80/%2E%2E/x",
                            "/etc/passwd",
                            "%2fetc/passwd",
                            "cpm80//x",
                            "cpm80/",
                            "cpm80/./x",
                            "cpm80/%00.txt",
                            "cpm80/x%00tail",
                            "%252e%252e/x",
                            "%",
                            "%0",
                            "%xy",
                            "cpm80\\x",
                            "cpm80%5cx",
                            "cpm80/a:",
                            "cpm80/foo.",
                            "cpm80/foo%20",
                            ".upload-part",
                            "cpm80/a%0ab",
                            "cpm80/a%22b",
                            "cpm80/%3Cscript%3E",
                            "cpm80/%7fx",
                            "cpm80/a+b",
                            "cpm80/x?y",
                            "cpm80/x#y"};
  char path[absolutePathCapacity];
  for (size_t index = 0; index < sizeof(rejected) / sizeof(rejected[0]); ++index)
  {
    assert(!pathResolve(rejected[index], path, sizeof(path)));
  }
  assert(pathResolve("", path, sizeof(path)));
  assert(strcmp(path, "/microSD/retro/exchange") == 0);
  assert(pathResolve("common/Hello%20World.bin", path, sizeof(path)));
  assert(strcmp(path, "/microSD/retro/exchange/common/Hello World.bin") == 0);
  assert(pathResolve("cpm80%2Fa.bin", path, sizeof(path)));
  assert(!pathResolve("common/a.bin", path, 8));
  assert(!pathDecode("x", path, 0));
  char longName[300];
  memset(longName, 'a', sizeof(longName));
  longName[64] = '\0';
  assert(pathIsSafe(longName));
  longName[64] = 'a';
  longName[65] = '\0';
  assert(!pathIsSafe(longName));
  unsigned seed = 12345;
  for (size_t iteration = 0; iteration < 50000; ++iteration)
  {
    char input[300];
    size_t length = iteration % sizeof(input);
    for (size_t index = 0; index < length; ++index)
    {
      seed = seed * 1664525U + 1013904223U;
      input[index] = (char)(1 + ((seed >> 16) % 255));
    }
    input[length] = '\0';
    if (pathResolve(input, path, sizeof(path)))
    {
      assert(strncmp(path, "/microSD/retro/exchange", 23) == 0);
      assert(strstr(path, "/../") == NULL);
      assert(strlen(path) < sizeof(path));
    }
  }
}

static uint32_t testCrc32(const uint8_t *data, size_t length)
{
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; ++index)
  {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit)
    {
      crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0);
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

static void writeCpm80Filename(hostExchange *exchange, const char *name, const char *extension)
{
  uint8_t rawName[11];
  memset(rawName, ' ', sizeof(rawName));
  memcpy(rawName, name, strlen(name));
  memcpy(rawName + 8, extension, strlen(extension));
  for (size_t index = 0; index < sizeof(rawName); ++index)
  {
    hostExchangePortOutput(exchange, hostExchangePort, rawName[index]);
  }
}

static void testHostExchange(void)
{
  char directoryPath[] = "/tmp/cpm80-exchange-test-XXXXXX";
  assert(mkdtemp(directoryPath) != NULL);
  char sourcePath[absolutePathCapacity];
  assert(snprintf(sourcePath, sizeof(sourcePath), "%s/ALPHA.BIN", directoryPath) <
         (int)sizeof(sourcePath));
  const uint8_t sourceBytes[] = {0x00, 0x1A, 0x7F, 0x80, 0xFF, 0x0D, 0x0A};
  FILE *source = fopen(sourcePath, "wb");
  assert(source != NULL);
  assert(fwrite(sourceBytes, 1, sizeof(sourceBytes), source) == sizeof(sourceBytes));
  assert(fclose(source) == 0);

  hostExchange exchange = {0};
  assert(!hostExchangeInitialize(&exchange, "/"));
  assert(!hostExchangeInitialize(&exchange, "/tmp/../"));
  assert(hostExchangeInitialize(&exchange, directoryPath));

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandQuery);
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  assert(hostExchangePortInput(&exchange, hostExchangePort) == 1);
  assert(hostExchangePortInput(&exchange, hostExchangePort) == 0x07);

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandDirectory);
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  char listedName[13];
  size_t listedLength = 0;
  uint8_t character;
  while ((character = hostExchangePortInput(&exchange, hostExchangePort)) != 0)
  {
    assert(listedLength + 1 < sizeof(listedName));
    listedName[listedLength++] = (char)character;
  }
  listedName[listedLength] = '\0';
  assert(strcmp(listedName, "ALPHA.BIN") == 0);
  assert(hostExchangePortInput(&exchange, hostExchangePort) == 0);
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandGet);
  writeCpm80Filename(&exchange, "MISSING", "BIN");
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusUnavailable);

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandGet);
  writeCpm80Filename(&exchange, "ALPHA", "BIN");
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  uint32_t sourceLength = 0;
  for (uint8_t index = 0; index < 4; ++index)
  {
    sourceLength |= (uint32_t)hostExchangePortInput(&exchange, hostExchangePort) << (index * 8);
  }
  assert(sourceLength == sizeof(sourceBytes));
  uint8_t received[sizeof(sourceBytes)];
  for (size_t index = 0; index < sizeof(received); ++index)
  {
    received[index] = hostExchangePortInput(&exchange, hostExchangePort);
  }
  uint32_t receivedCrc = 0;
  for (uint8_t index = 0; index < 4; ++index)
  {
    receivedCrc |= (uint32_t)hostExchangePortInput(&exchange, hostExchangePort) << (index * 8);
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  assert(memcmp(received, sourceBytes, sizeof(received)) == 0);
  assert(receivedCrc == testCrc32(sourceBytes, sizeof(sourceBytes)));

  const uint8_t destinationBytes[] = {0xA5, 0x00, 0x1A, 0xFE, 0x55};
  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandPut);
  writeCpm80Filename(&exchange, "RESULT", "TXT");
  for (uint8_t index = 0; index < 4; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, (uint8_t)(sizeof(destinationBytes) >> (index * 8)));
  }
  uint32_t destinationCrc = testCrc32(destinationBytes, sizeof(destinationBytes));
  for (uint8_t index = 0; index < 4; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, (uint8_t)(destinationCrc >> (index * 8)));
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  for (size_t index = 0; index < sizeof(destinationBytes); ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, destinationBytes[index]);
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  char destinationPath[absolutePathCapacity];
  assert(snprintf(destinationPath, sizeof(destinationPath), "%s/RESULT.TXT", directoryPath) <
         (int)sizeof(destinationPath));
  uint8_t actualBytes[sizeof(destinationBytes)];
  FILE *destination = fopen(destinationPath, "rb");
  assert(destination != NULL);
  assert(fread(actualBytes, 1, sizeof(actualBytes), destination) == sizeof(actualBytes));
  assert(fgetc(destination) == EOF);
  assert(fclose(destination) == 0);
  assert(memcmp(actualBytes, destinationBytes, sizeof(actualBytes)) == 0);

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandPut);
  writeCpm80Filename(&exchange, "BADCRC", "BIN");
  for (uint8_t index = 0; index < 4; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, (uint8_t)(sizeof(destinationBytes) >> (index * 8)));
  }
  for (uint8_t index = 0; index < 4; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort,
                           (uint8_t)((destinationCrc ^ 1U) >> (index * 8)));
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  for (size_t index = 0; index < sizeof(destinationBytes); ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, destinationBytes[index]);
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusIoError);
  char badCrcPath[absolutePathCapacity];
  assert(snprintf(badCrcPath, sizeof(badCrcPath), "%s/BADCRC.BIN", directoryPath) <
         (int)sizeof(badCrcPath));
  struct stat info;
  assert(stat(badCrcPath, &info) != 0);

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandPut);
  writeCpm80Filename(&exchange, "EMPTY", "DAT");
  for (uint8_t index = 0; index < 8; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, 0);
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  char emptyPath[absolutePathCapacity];
  assert(snprintf(emptyPath, sizeof(emptyPath), "%s/EMPTY.DAT", directoryPath) < (int)sizeof(emptyPath));
  FILE *empty = fopen(emptyPath, "rb");
  assert(empty != NULL);
  assert(fgetc(empty) == EOF);
  assert(fclose(empty) == 0);

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandPut);
  writeCpm80Filename(&exchange, "RESULT", "TXT");
  for (uint8_t index = 0; index < 4; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, (uint8_t)(sizeof(destinationBytes) >> (index * 8)));
  }
  for (uint8_t index = 0; index < 4; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, (uint8_t)(destinationCrc >> (index * 8)));
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusExists);

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandGet);
  const uint8_t invalidName[11] = {'/', '.', '.', '.', '.', '.', '.', '.', 'T', 'X', 'T'};
  for (size_t index = 0; index < sizeof(invalidName); ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, invalidName[index]);
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusInvalid);

  hostExchangePortOutput(&exchange, hostExchangePort, hostExchangeCommandPut);
  writeCpm80Filename(&exchange, "PARTIAL", "TMP");
  for (uint8_t index = 0; index < 4; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, 10);
  }
  for (uint8_t index = 0; index < 4; ++index)
  {
    hostExchangePortOutput(&exchange, hostExchangePort, 0);
  }
  assert(hostExchangePortInput(&exchange, hostExchangePort) == hostExchangeStatusOk);
  hostExchangePortOutput(&exchange, hostExchangePort, 0xCC);
  hostExchangePortOutput(&exchange, hostExchangeAbortPort, 0);
  char partialPath[absolutePathCapacity];
  assert(snprintf(partialPath, sizeof(partialPath), "%s/PARTIAL.TMP", directoryPath) <
         (int)sizeof(partialPath));
  assert(stat(partialPath, &info) != 0);

  hostExchangeClose(&exchange);
  assert(unlink(sourcePath) == 0);
  assert(unlink(destinationPath) == 0);
  assert(unlink(emptyPath) == 0);
  assert(rmdir(directoryPath) == 0);
}

static void testLayout(void)
{
  const char *valid[] = {"ESP32-S3-RETRO\nlayout=1\n", "ESP32-S3-RETRO\r\nlayout=1\r\n", "ESP32-S3-RETRO\rlayout=1\r",
                         "ESP32-S3-RETRO\nlayout=1", "ESP32-S3-RETRO\r\nlayout=1\n"};
  for (size_t index = 0; index < sizeof(valid) / sizeof(valid[0]); ++index)
  {
    assert(layoutIsValid(valid[index], strlen(valid[index])));
  }
  const char *invalid[] = {"",
                           "layout=1\n",
                           "ESP32-S3-RETRO\nlayout=2\n",
                           "ESP32-S3-RETRO\nlayout=1\nextra",
                           "ESP32-S3-RETROlayout=1",
                           "ESP32-S3-RETRO\nlayout=1\n\n"};
  for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
  {
    assert(!layoutIsValid(invalid[index], strlen(invalid[index])));
  }
}

static void testMenu(void)
{
  menuLine line = {0};
  bool valid;
  assert(!menuFeed(&line, '1', &valid));
  assert(menuFeed(&line, '\r', &valid) && valid);
  assert(strcmp(line.text, "1") == 0);
  assert(!menuFeed(&line, '\n', &valid));
  assert(menuFeed(&line, '\n', &valid) && valid);
  assert(strcmp(line.text, "") == 0);
  assert(!menuFeed(&line, '2', &valid));
  assert(!menuFeed(&line, 127, &valid));
  assert(!menuFeed(&line, '3', &valid));
  assert(menuFeed(&line, '\n', &valid) && valid);
  assert(strcmp(line.text, "3") == 0);
  for (size_t index = 0; index < 100; ++index)
  {
    assert(!menuFeed(&line, '1', &valid));
  }
  assert(menuFeed(&line, '\r', &valid) && !valid);
  assert(!menuFeed(&line, '\n', &valid));
  assert(!menuFeed(&line, '6', &valid));
  assert(menuFeed(&line, '\n', &valid) && valid);
  assert(strcmp(line.text, "6") == 0);
  assert(!menuFeed(&line, 27, &valid));
  assert(menuFeed(&line, '\n', &valid) && !valid);
}

static void testImages(void)
{
  char filePath[] = "image-test-XXXXXX";
  int descriptor = mkstemp(filePath);
  assert(descriptor >= 0);
  assert(ftruncate(descriptor, 64 * 1024 * 1024) == 0);
  close(descriptor);
  imageFile image = {0};
  assert(imageOpen(&image, filePath, false));
  assert(imageSize(&image) == 64 * 1024 * 1024);
  const unsigned char expected[] = {0, 13, 10, 26, 127, 128, 255};
  unsigned char actual[sizeof(expected)] = {0};
  assert(imageWriteAt(&image, image.size - sizeof(expected), expected, sizeof(expected)));
  assert(imageFlush(&image));
  assert(imageReadAt(&image, image.size - sizeof(expected), actual, sizeof(actual)));
  assert(memcmp(actual, expected, sizeof(expected)) == 0);
  assert(!imageWriteAt(&image, image.size - 1, expected, sizeof(expected)));
  assert(!imageReadAt(&image, UINT64_MAX, actual, sizeof(actual)));
  assert(!imageReadAt(&image, image.size, actual, 1));
  assert(imageClose(&image));
  assert(imageOpen(&image, filePath, true));
  assert(!imageWriteAt(&image, 0, expected, sizeof(expected)));
  assert(imageClose(&image));
  unlink(filePath);
}

typedef struct
{
  uint8_t inputValue;
  uint8_t inputPort;
  uint8_t outputValue;
  uint8_t outputPort;
} portFixture;

static uint8_t testPortInput(void *context, uint8_t port)
{
  portFixture *fixture = context;
  fixture->inputPort = port;
  return fixture->inputValue;
}

static void testPortOutput(void *context, uint8_t port, uint8_t value)
{
  portFixture *fixture = context;
  fixture->outputPort = port;
  fixture->outputValue = value;
}

static void testCpm80DriveConfig(void)
{
  static const char configText[] =
      "A=/littlefs/cpm80/system.dsk,RO,SYSTEM\r\n"
      "B=/retro/images/cpm80/languages.dsk,RO,SYSTEM\n"
      "C=/retro/images/cpm80/tools.dsk,RO,LARGE\n"
      "D=/retro/images/cpm80/utilities.dsk,RO,LARGE\n"
      "E=/retro/images/cpm80/work.dsk,RW,LARGE\n"
      "F=/retro/images/cpm80/archive.dsk,RW,LARGE\n";
  char configPath[] = "/tmp/cpm80-drives-config-test-XXXXXX";
  int descriptor = mkstemp(configPath);
  assert(descriptor >= 0);
  FILE *file = fdopen(descriptor, "wb");
  assert(file != NULL);
  assert(fwrite(configText, 1, sizeof(configText) - 1, file) == sizeof(configText) - 1);
  assert(fclose(file) == 0);

  cpm80DriveConfig drives[cpm80DiskDriveCount];
  char error[96];
  assert(cpm80DriveConfigLoad(configPath, drives, error, sizeof(error)) == cpm80DriveConfigLoaded);
  assert(drives[0].configured && drives[0].profile == cpm80DiskProfileSystem && drives[0].readOnly);
  assert(strcmp(drives[1].path, "/microSD/retro/images/cpm80/languages.dsk") == 0);
  assert(drives[1].configured && drives[1].profile == cpm80DiskProfileSystem && drives[1].readOnly);
  assert(strcmp(drives[5].path, "/microSD/retro/images/cpm80/archive.dsk") == 0);
  assert(drives[5].configured && drives[5].profile == cpm80DiskProfileLarge && !drives[5].readOnly);

  file = fopen(configPath, "wb");
  assert(file != NULL);
  static const char invalidConfig[] = "B=/retro/images/cpm80/../escape.dsk,RW,LARGE\n";
  assert(fwrite(invalidConfig, 1, sizeof(invalidConfig) - 1, file) == sizeof(invalidConfig) - 1);
  assert(fclose(file) == 0);
  assert(cpm80DriveConfigLoad(configPath, drives, error, sizeof(error)) == cpm80DriveConfigInvalid);
  assert(drives[0].configured && drives[0].profile == cpm80DiskProfileSystem && drives[0].readOnly);
  for (uint8_t drive = 1; drive < cpm80DiskDriveCount; ++drive)
  {
    assert(!drives[drive].configured);
  }
  assert(unlink(configPath) == 0);

  assert(cpm80DriveConfigLoad(configPath, drives, error, sizeof(error)) == cpm80DriveConfigMissing);
  assert(drives[0].configured && drives[0].profile == cpm80DiskProfileSystem && drives[0].readOnly);
  for (uint8_t drive = 1; drive < cpm80DiskDriveCount; ++drive)
  {
    assert(!drives[drive].configured);
  }
}

static void testCpm80Cpu(void)
{
  cpm80Cpu cpu = {0};
  portFixture ports = {.inputValue = 0xA5};
  const uint8_t program[] = {0x3E, 0x5A, 0x32, 0xFF, 0xFF, 0xDB, 0x42, 0xD3, 0x43, 0x76};
  uint8_t value;

  assert(!cpm80CpuInitialize(NULL, testPortInput, testPortOutput, &ports));
  assert(!cpm80CpuInitialize(&cpu, NULL, testPortOutput, &ports));
  assert(!cpm80CpuInitialize(&cpu, testPortInput, NULL, &ports));
  assert(cpm80CpuInitialize(&cpu, testPortInput, testPortOutput, &ports));
  assert(cpm80CpuLoad(&cpu, 0, program, sizeof(program)));
  assert(!cpm80CpuLoad(&cpu, UINT16_MAX, program, 2));
  assert(!cpm80CpuLoad(&cpu, 0, NULL, 1));
  assert(cpm80CpuWriteMemory(&cpu, UINT16_MAX, 0xC3));
  assert(cpm80CpuReadMemory(&cpu, UINT16_MAX, &value) && value == 0xC3);
  assert(!cpm80CpuReadMemory(&cpu, 0, NULL));

  assert(cpm80CpuStep(&cpu));
  assert(cpm80CpuStep(&cpu));
  assert(cpm80CpuReadMemory(&cpu, UINT16_MAX, &value) && value == 0x5A);
  assert(cpm80CpuStep(&cpu));
  assert(cpu.processor.a == 0xA5 && ports.inputPort == 0x42);
  assert(cpm80CpuStep(&cpu));
  assert(ports.outputPort == 0x43 && ports.outputValue == 0xA5);
  assert(cpm80CpuStep(&cpu) && cpu.processor.halted);
  assert(cpu.processor.pc == sizeof(program));
  assert(!cpm80CpuStep(NULL));
  cpm80CpuDestroy(&cpu);
  assert(!cpu.initialized && cpu.memory == NULL);
}

typedef struct
{
  imageFile *disks[cpm80DiskDriveCount];
  bool availableDrives[cpm80DiskDriveCount];
  bool writableDrives[cpm80DiskDriveCount];
  cpm80DiskProfile diskProfiles[cpm80DiskDriveCount];
  hostExchange *exchange;
  const char *input;
  size_t inputLength;
  size_t inputPosition;
  char output[8192];
  size_t outputLength;
  bool outputOverflow;
} cpm80GuestFixture;

static bool cpm80TestConsoleAvailable(void *context)
{
  cpm80GuestFixture *fixture = context;
  return fixture->inputPosition < fixture->inputLength;
}

static int cpm80TestConsoleRead(void *context)
{
  cpm80GuestFixture *fixture = context;
  return fixture->inputPosition < fixture->inputLength ? (unsigned char)fixture->input[fixture->inputPosition++] : -1;
}

static void cpm80TestConsoleWrite(void *context, uint8_t character)
{
  cpm80GuestFixture *fixture = context;
  if (fixture->outputLength + 1 >= sizeof(fixture->output))
  {
    fixture->outputOverflow = true;
    return;
  }
  fixture->output[fixture->outputLength++] = (char)character;
  fixture->output[fixture->outputLength] = '\0';
}

static bool cpm80TestDiskDriveAvailable(void *context, uint8_t drive)
{
  cpm80GuestFixture *fixture = context;
  return drive < cpm80DiskDriveCount && fixture->availableDrives[drive];
}

static bool cpm80TestDiskDriveProfile(void *context, uint8_t drive, cpm80DiskProfile *profile)
{
  cpm80GuestFixture *fixture = context;
  if (drive >= cpm80DiskDriveCount || profile == NULL || !fixture->availableDrives[drive])
  {
    return false;
  }
  *profile = fixture->diskProfiles[drive];
  return true;
}

static bool cpm80TestDiskRead(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                            uint8_t record[cpm80DiskSectorSize])
{
  cpm80GuestFixture *fixture = context;
  if (drive >= cpm80DiskDriveCount)
  {
    return false;
  }
  uint16_t sectorsPerTrack = fixture->diskProfiles[drive] == cpm80DiskProfileLarge
                                 ? cpm80LargeDiskSectorsPerTrack
                                 : cpm80DiskSectorsPerTrack;
  if (fixture->disks[drive] == NULL || track >= cpm80DiskTracks || sector >= sectorsPerTrack)
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)track * sectorsPerTrack + sector;
  return imageReadAt(fixture->disks[drive], recordIndex * cpm80DiskSectorSize, record, cpm80DiskSectorSize);
}

static bool cpm80TestDiskWrite(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                             const uint8_t record[cpm80DiskSectorSize])
{
  cpm80GuestFixture *fixture = context;
  if (drive >= cpm80DiskDriveCount)
  {
    return false;
  }
  uint16_t sectorsPerTrack = fixture->diskProfiles[drive] == cpm80DiskProfileLarge
                                 ? cpm80LargeDiskSectorsPerTrack
                                 : cpm80DiskSectorsPerTrack;
  if (fixture->disks[drive] == NULL || !fixture->writableDrives[drive] || track >= cpm80DiskTracks ||
      sector >= sectorsPerTrack)
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)track * sectorsPerTrack + sector;
  return imageWriteAt(fixture->disks[drive], recordIndex * cpm80DiskSectorSize, record, cpm80DiskSectorSize) &&
         imageFlush(fixture->disks[drive]);
}

static void cpm80TestYield(void *context)
{
  (void)context;
}

static uint8_t cpm80TestExchangePortInput(void *context, uint8_t port)
{
  cpm80GuestFixture *fixture = context;
  return hostExchangePortInput(fixture->exchange, port);
}

static void cpm80TestExchangePortOutput(void *context, uint8_t port, uint8_t value)
{
  cpm80GuestFixture *fixture = context;
  hostExchangePortOutput(fixture->exchange, port, value);
}

static size_t countOccurrences(const char *text, const char *needle)
{
  size_t count = 0;
  size_t needleLength = strlen(needle);
  while ((text = strstr(text, needle)) != NULL)
  {
    ++count;
    text += needleLength;
  }
  return count;
}

static void testCpm80GuestBoot(void)
{
  char exchangeDirectoryPath[] = "/tmp/cpm80-guest-exchange-test-XXXXXX";
  assert(mkdtemp(exchangeDirectoryPath) != NULL);
  hostExchange exchange = {0};
  assert(hostExchangeInitialize(&exchange, exchangeDirectoryPath));
  char exchangeFilePath[absolutePathCapacity];
  assert(snprintf(exchangeFilePath, sizeof(exchangeFilePath), "%s/INPUT.BIN", exchangeDirectoryPath) <
         (int)sizeof(exchangeFilePath));
  uint8_t exchangeFileBytes[777];
  for (size_t index = 0; index < sizeof(exchangeFileBytes); ++index)
  {
    exchangeFileBytes[index] = (uint8_t)(index * 37U + 11U);
  }
  exchangeFileBytes[0] = 0x00;
  exchangeFileBytes[1] = 0x1A;
  exchangeFileBytes[2] = 0x7F;
  exchangeFileBytes[3] = 0x80;
  exchangeFileBytes[4] = 0xFF;
  exchangeFileBytes[5] = 0x0D;
  exchangeFileBytes[6] = 0x0A;
  FILE *exchangeFile = fopen(exchangeFilePath, "wb");
  assert(exchangeFile != NULL);
  assert(fwrite(exchangeFileBytes, 1, sizeof(exchangeFileBytes), exchangeFile) == sizeof(exchangeFileBytes));
  assert(fclose(exchangeFile) == 0);
  imageFile disk = {0};
  char writableDiskPath[] = "cpm-write-test-XXXXXX";
  int writableDiskDescriptor = mkstemp(writableDiskPath);
  assert(writableDiskDescriptor >= 0);
  assert(ftruncate(writableDiskDescriptor, cpm80SystemImageSize) == 0);
  close(writableDiskDescriptor);
  imageFile writableDisk = {0};
  assert(imageOpen(&writableDisk, writableDiskPath, false));
  uint8_t emptyDirectory[cpm80DiskDirectoryEntries * 32];
  memset(emptyDirectory, 0xE5, sizeof(emptyDirectory));
  uint64_t directoryOffset = 2U * cpm80DiskSectorsPerTrack * cpm80DiskSectorSize;
  assert(imageWriteAt(&writableDisk, directoryOffset, emptyDirectory, sizeof(emptyDirectory)));
  assert(imageFlush(&writableDisk));
  char largeDiskPath[] = "cpm-large-write-test-XXXXXX";
  int largeDiskDescriptor = mkstemp(largeDiskPath);
  assert(largeDiskDescriptor >= 0);
  assert(ftruncate(largeDiskDescriptor, cpm80LargeImageSize) == 0);
  close(largeDiskDescriptor);
  imageFile largeDisk = {0};
  assert(imageOpen(&largeDisk, largeDiskPath, false));
  uint8_t largeEmptyDirectory[cpm80LargeDiskDirectoryEntries * 32];
  memset(largeEmptyDirectory, 0xE5, sizeof(largeEmptyDirectory));
  uint64_t largeDirectoryOffset = 2U * cpm80LargeDiskSectorsPerTrack * cpm80DiskSectorSize;
  assert(imageWriteAt(&largeDisk, largeDirectoryOffset, largeEmptyDirectory, sizeof(largeEmptyDirectory)));
  assert(imageFlush(&largeDisk));
  assert(imageOpen(&disk, CPM80_SYSTEM_IMAGE_PATH, true));
  assert(imageSize(&disk) == cpm80SystemImageSize);
  uint8_t ccpImage[cpm80CcpSize];
  uint8_t bdosImage[cpm80BdosSize];
  assert(imageReadAt(&disk, 0, ccpImage, sizeof(ccpImage)));
  assert(imageReadAt(&disk, sizeof(ccpImage), bdosImage, sizeof(bdosImage)));

  cpm80GuestFixture fixture = {.disks = {[0] = &disk, [1] = &disk, [4] = &writableDisk, [5] = &largeDisk},
                             .availableDrives = {[0] = true, [1] = true, [4] = true, [5] = true},
                             .writableDrives = {[4] = true, [5] = true},
                             .diskProfiles = {[5] = cpm80DiskProfileLarge},
                             .exchange = &exchange};
  const cpm80HostOps host = {.consoleAvailable = cpm80TestConsoleAvailable,
                           .consoleRead = cpm80TestConsoleRead,
                           .consoleWrite = cpm80TestConsoleWrite,
                           .diskDriveAvailable = cpm80TestDiskDriveAvailable,
                           .diskDriveProfile = cpm80TestDiskDriveProfile,
                           .diskReadRecord = cpm80TestDiskRead,
                           .diskWriteRecord = cpm80TestDiskWrite,
                           .exchangePortInput = cpm80TestExchangePortInput,
                           .exchangePortOutput = cpm80TestExchangePortOutput,
                           .yield = cpm80TestYield,
                           .context = &fixture};
  cpm80Guest guest = {0};
  assert(cpm80GuestInitialize(&guest, &host, ccpImage, bdosImage));
  const uint8_t exchangePortProgram[] = {
      0x3E, hostExchangeCommandDirectory, 0xD3, hostExchangePort, 0xDB, hostExchangePort,
      0x32, 0xFF, 0x02, 0x3E, 0x00, 0xD3, hostExchangeAbortPort, 0x76};
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, exchangePortProgram, sizeof(exchangePortProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0200;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.cpu.memory[0x02FF] == hostExchangeStatusOk);
  assert(cpm80GuestColdBoot(&guest));
  assert(guest.cpu.memory[0x0000] == 0xC3 && guest.cpu.memory[0x0001] == 0x03 && guest.cpu.memory[0x0002] == 0xDA);
  assert(guest.cpu.memory[0x0005] == 0xC3 && guest.cpu.memory[0x0006] == 0x06 && guest.cpu.memory[0x0007] == 0xCC);
  assert(guest.cpu.memory[0xDA90 + 10] == 0xA0 && guest.cpu.memory[0xDA90 + 11] == 0xDA);
  assert(guest.cpu.memory[0xDAA0] == 26 && guest.cpu.memory[0xDAA1] == 0);
  assert(guest.cpu.memory[0xDAA2] == 3 && guest.cpu.memory[0xDAA3] == 7);
  assert(guest.cpu.memory[0xDAA4] == 0 && guest.cpu.memory[0xDAA5] == 242);
  assert(guest.cpu.memory[0xDAA6] == 0 && guest.cpu.memory[0xDAA7] == 63);
  assert(guest.cpu.memory[0xDAA8] == 0 && guest.cpu.memory[0xDAA9] == 192);
  assert(guest.cpu.memory[0xDAAA] == 0 && guest.cpu.memory[0xDAAB] == 16);
  assert(guest.cpu.memory[0xDAAC] == 0 && guest.cpu.memory[0xDAAD] == 2);
  const uint16_t dphAddresses[cpm80DiskDriveCount] = {0xDA90, 0xDB60, 0xDC40, 0xDD20, 0xDE00, 0xDEE0};
  for (uint8_t drive = 1; drive < cpm80DiskDriveCount; ++drive)
  {
    uint16_t dphAddress = dphAddresses[drive];
    assert(guest.cpu.memory[dphAddress + 8] == (uint8_t)(dphAddress + 0x20));
    assert(guest.cpu.memory[dphAddress + 9] == (uint8_t)((dphAddress + 0x20) >> 8));
    assert(guest.cpu.memory[dphAddress + 10] == (uint8_t)(dphAddress + 0x10));
    assert(guest.cpu.memory[dphAddress + 11] == (uint8_t)((dphAddress + 0x10) >> 8));
    assert(guest.cpu.memory[dphAddress + 12] == (uint8_t)(dphAddress + 0xA0));
    assert(guest.cpu.memory[dphAddress + 13] == (uint8_t)((dphAddress + 0xA0) >> 8));
    assert(guest.cpu.memory[dphAddress + 14] == (uint8_t)(dphAddress + 0xC0));
    assert(guest.cpu.memory[dphAddress + 15] == (uint8_t)((dphAddress + 0xC0) >> 8));
  }
  assert(guest.cpu.memory[0xDEF0] == 52 && guest.cpu.memory[0xDEF1] == 0);
  assert(guest.cpu.memory[0xDEF2] == 4 && guest.cpu.memory[0xDEF3] == 15);
  assert(guest.cpu.memory[0xDEF4] == 0 && guest.cpu.memory[0xDEF5] == 242);
  assert(guest.cpu.memory[0xDEF6] == 0 && guest.cpu.memory[0xDEF7] == 127);
  assert(guest.cpu.memory[0xDEFA] == 0 && guest.cpu.memory[0xDEFB] == 32);

  size_t instructions = 0;
  size_t priorSystemPrompts;
  size_t priorWorkPrompts;
  while (countOccurrences(fixture.output, "A>") < 1 && instructions < 500000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(countOccurrences(fixture.output, "A>") == 1);

  fixture.input = "DIR\r";
  fixture.inputLength = 4;
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < 2 && instructions < 2000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(countOccurrences(fixture.output, "A>") == 2);
  assert(strstr(fixture.output, "HELLO    COM") != NULL);
  const char *expectedUtilities[] = {
      "ASM.COM", "DDT.COM", "DUMP.COM", "ED.COM", "HELP.COM", "HELP.HLP", "LIB.COM", "LINK.COM",
      "HOST.COM", "LOAD.COM", "MAC.COM", "PIP.COM", "RMAC.COM", "STAT.COM", "SUBMIT.COM", "WELCOME.TXT",
      "XREF.COM",
      "XSUB.COM", "ZSID.COM",
  };
  for (size_t index = 0; index < sizeof(expectedUtilities) / sizeof(expectedUtilities[0]); ++index)
  {
    const char *extension = strchr(expectedUtilities[index], '.');
    assert(extension != NULL);
    char name[9] = {0};
    size_t nameLength = (size_t)(extension - expectedUtilities[index]);
    assert(nameLength <= 8);
    memcpy(name, expectedUtilities[index], nameLength);
    char expectedEntry[13];
    assert(snprintf(expectedEntry, sizeof(expectedEntry), "%-8s %s", name, extension + 1) <
           (int)sizeof(expectedEntry));
    assert(strstr(fixture.output, expectedEntry) != NULL);
  }

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "HOST DIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 &&
         instructions < 6000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 6000000);
  assert(strstr(fixture.output, "INPUT.BIN") != NULL);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "E:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 7000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 7000000);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "A:HOST GET INPUT.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 &&
         instructions < 10000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 10000000);
  assert(strstr(fixture.output, "HOST: GET complete") != NULL);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "E:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 11000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 11000000);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "A:PIP E:INPUT2.HST=E:INPUT.HST\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 &&
         instructions < 12000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 12000000);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "E:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 13000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 13000000);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "A:HOST GET INPUT2.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 &&
         instructions < 15000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 15000000);
  assert(strstr(fixture.output, "HOST: metadata file (.HST) already exists") != NULL);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "E:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 16000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 16000000);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "DIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 17000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 17000000);
  assert(strstr(fixture.output, "INPUT2   HST") != NULL);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "A:HOST GET INPUT.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 &&
         instructions < 18000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 18000000);
  assert(strstr(fixture.output, "HOST: target already exists") != NULL);
  assert(unlink(exchangeFilePath) == 0);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "E:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 19000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 19000000);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "A:HOST PUT INPUT.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 &&
         instructions < 22000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 22000000);
  assert(strstr(fixture.output, "HOST: PUT complete") != NULL);
  exchangeFile = fopen(exchangeFilePath, "rb");
  assert(exchangeFile != NULL);
  uint8_t roundTripBytes[sizeof(exchangeFileBytes)];
  assert(fread(roundTripBytes, 1, sizeof(roundTripBytes), exchangeFile) == sizeof(roundTripBytes));
  assert(fgetc(exchangeFile) == EOF);
  assert(fclose(exchangeFile) == 0);
  assert(memcmp(roundTripBytes, exchangeFileBytes, sizeof(roundTripBytes)) == 0);
  instructions = 0;

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "TYPE WELCOME.TXT\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 &&
         instructions < 5000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 5000000);
  assert(strstr(fixture.output, "CP/M utilities are installed.") != NULL);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "USER 1\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 && instructions < 6000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 6000000);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "DIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while ((countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 ||
          strstr(fixture.output, "NO FILE") == NULL) &&
         instructions < 6500000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 6500000);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "USER 0\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 && instructions < 7000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 7000000);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  size_t userDirectoryEntries = countOccurrences(fixture.output, "HELLO    COM");
  fixture.input = "DIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while ((countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 ||
          countOccurrences(fixture.output, "HELLO    COM") == userDirectoryEntries) &&
         instructions < 7500000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 7500000);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "HELLO\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while ((countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 ||
          strstr(fixture.output, "HELLO FROM CP/M-80") == NULL) &&
         instructions < 7000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 7000000);
  assert(strstr(fixture.output, "HELLO FROM CP/M-80") != NULL);
  assert(!fixture.outputOverflow);

  size_t priorDirectoryEntries = countOccurrences(fixture.output, "HELLO    COM");
  size_t priorDrivePrompts = countOccurrences(fixture.output, "B>");
  fixture.input = "B:\rDIR\r";
  fixture.inputLength = 7;
  fixture.inputPosition = 0;
  while ((countOccurrences(fixture.output, "B>") < priorDrivePrompts + 2 ||
          countOccurrences(fixture.output, "HELLO    COM") == priorDirectoryEntries) &&
         instructions < 7000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 7000000);
  assert(strstr(fixture.output, "B>") != NULL);
  assert(countOccurrences(fixture.output, "HELLO    COM") > priorDirectoryEntries);

  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "PIP E:=A:HELLO.COM\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 1 && instructions < 12000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 12000000);
  priorDirectoryEntries = countOccurrences(fixture.output, "HELLO    COM");
  fixture.input = "E:\rDIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  while ((countOccurrences(fixture.output, "E>") < priorWorkPrompts + 2 ||
          countOccurrences(fixture.output, "HELLO    COM") == priorDirectoryEntries) &&
         instructions < 15000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 15000000);
  assert(countOccurrences(fixture.output, "HELLO    COM") > priorDirectoryEntries);
  assert(!fixture.outputOverflow);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "REN GREETING.COM=HELLO.COM\rDIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while ((countOccurrences(fixture.output, "E>") < priorWorkPrompts + 2 ||
          strstr(fixture.output, "GREETING COM") == NULL) &&
         instructions < 18000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 18000000);
  assert(strstr(fixture.output, "GREETING COM") != NULL);

  size_t priorGreetingEntries = countOccurrences(fixture.output, "GREETING COM");
  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "ERA GREETING.COM\rDIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 2 && instructions < 21000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 21000000);
  assert(countOccurrences(fixture.output, "GREETING COM") == priorGreetingEntries);

  size_t priorLargeNoFiles = countOccurrences(fixture.output, "NO FILE");
  size_t priorLargePrompts = countOccurrences(fixture.output, "F>");
  fixture.input = "F:\rDIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while ((countOccurrences(fixture.output, "F>") < priorLargePrompts + 2 ||
          countOccurrences(fixture.output, "NO FILE") == priorLargeNoFiles) &&
         instructions < 23000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 23000000);

  const uint8_t selectDriveProgram[] = {0x0E, 0x01, 0xCD, 0x1B, 0xDA, 0x76};
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, selectDriveProgram, sizeof(selectDriveProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0200;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 1);
  assert(guest.cpu.processor.h == 0xDB && guest.cpu.processor.l == 0x60);

  const uint8_t rejectUnavailableDriveProgram[] = {0x0E, 0x02, 0xCD, 0x1B, 0xDA, 0x76};
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, rejectUnavailableDriveProgram, sizeof(rejectUnavailableDriveProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0200;
  guest.cpu.processor.halted = false;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 1);
  assert(guest.cpu.processor.h == 0 && guest.cpu.processor.l == 0);

  fixture.availableDrives[4] = true;
  const uint8_t selectLastDriveProgram[] = {0x0E, 0x05, 0xCD, 0x1B, 0xDA, 0x76};
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, selectLastDriveProgram, sizeof(selectLastDriveProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0200;
  guest.cpu.processor.halted = false;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 5);
  assert(guest.cpu.processor.h == 0xDE && guest.cpu.processor.l == 0xE0);

  const uint8_t rejectOutOfRangeDriveProgram[] = {0x0E, 0x06, 0xCD, 0x1B, 0xDA, 0x76};
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, rejectOutOfRangeDriveProgram, sizeof(rejectOutOfRangeDriveProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0200;
  guest.cpu.processor.halted = false;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 5);
  assert(guest.cpu.processor.h == 0 && guest.cpu.processor.l == 0);

  const uint8_t writeDiskRecordProgram[] = {
      0x0E, 0x04, 0xCD, 0x1B, 0xDA, 0x01, 0x00, 0x00, 0xCD, 0x1E, 0xDA, 0x01, 0x00,
      0x00, 0xCD, 0x21, 0xDA, 0x01, 0x00, 0x02, 0xCD, 0x24, 0xDA, 0xCD, 0x2A, 0xDA, 0x76};
  const uint8_t expectedRecordPrefix[] = {0x5A, 0xA5, 0xC3, 0x3C};
  memcpy(guest.cpu.memory + 0x0200, expectedRecordPrefix, sizeof(expectedRecordPrefix));
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, writeDiskRecordProgram, sizeof(writeDiskRecordProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0300;
  guest.cpu.processor.halted = false;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 4 && guest.cpu.processor.a == 0);
  uint8_t actualRecord[cpm80DiskSectorSize];
  assert(imageReadAt(&writableDisk, 0, actualRecord, sizeof(actualRecord)));
  assert(memcmp(actualRecord, expectedRecordPrefix, sizeof(expectedRecordPrefix)) == 0);

  const uint8_t writeLargeDiskRecordProgram[] = {
      0x0E, 0x05, 0xCD, 0x1B, 0xDA, 0x01, 0x4C, 0x00, 0xCD, 0x1E, 0xDA, 0x01, 0x33,
      0x00, 0xCD, 0x21, 0xDA, 0x01, 0x00, 0x02, 0xCD, 0x24, 0xDA, 0xCD, 0x2A, 0xDA, 0x76};
  memcpy(guest.cpu.memory + 0x0200, expectedRecordPrefix, sizeof(expectedRecordPrefix));
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, writeLargeDiskRecordProgram, sizeof(writeLargeDiskRecordProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0300;
  guest.cpu.processor.halted = false;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 5 && guest.cpu.processor.a == 0);
  uint64_t lastLargeRecordOffset =
      ((uint64_t)(cpm80DiskTracks - 1) * cpm80LargeDiskSectorsPerTrack + (cpm80LargeDiskSectorsPerTrack - 1)) *
      cpm80DiskSectorSize;
  assert(imageReadAt(&largeDisk, lastLargeRecordOffset, actualRecord, sizeof(actualRecord)));
  assert(memcmp(actualRecord, expectedRecordPrefix, sizeof(expectedRecordPrefix)) == 0);

  const uint8_t rejectReadOnlyWriteProgram[] = {
      0x0E, 0x00, 0xCD, 0x1B, 0xDA, 0x01, 0x00, 0x00, 0xCD, 0x1E, 0xDA, 0x01, 0x00,
      0x00, 0xCD, 0x21, 0xDA, 0x01, 0x00, 0x02, 0xCD, 0x24, 0xDA, 0xCD, 0x2A, 0xDA, 0x76};
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, rejectReadOnlyWriteProgram, sizeof(rejectReadOnlyWriteProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0300;
  guest.cpu.processor.halted = false;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 0 && guest.cpu.processor.a == 1);

  const uint8_t selectSystemDriveProgram[] = {0x0E, 0x00, 0xCD, 0x1B, 0xDA, 0x76};
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, selectSystemDriveProgram, sizeof(selectSystemDriveProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0200;
  guest.cpu.processor.halted = false;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 0);

  const uint8_t invalidDmaProgram[] = {0x0E, 0x01, 0xCD, 0x1B, 0xDA, 0x0E, 0x00, 0xCD, 0x1B, 0xDA,
                                      0x01, 0xC1, 0xFF, 0xCD, 0x24, 0xDA, 0xCD, 0x27, 0xDA, 0x76};
  assert(cpm80CpuLoad(&guest.cpu, 0x0100, invalidDmaProgram, sizeof(invalidDmaProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0200;
  guest.cpu.processor.halted = false;
  while (!guest.cpu.processor.halted)
  {
    assert(cpm80GuestStep(&guest));
  }
  assert(guest.selectedDrive == 0);
  assert(guest.cpu.processor.a == 1);
  assert(guest.cpu.processor.h == 0xDA && guest.cpu.processor.l == 0x90);

  cpm80GuestDestroy(&guest);
  assert(imageClose(&disk));
  assert(imageClose(&writableDisk));
  assert(imageClose(&largeDisk));
  unlink(writableDiskPath);
  unlink(largeDiskPath);
  hostExchangeClose(&exchange);
  assert(unlink(exchangeFilePath) == 0);
  assert(rmdir(exchangeDirectoryPath) == 0);
}

int main(void)
{
  testPaths();
  testHostExchange();
  testLayout();
  testMenu();
  testImages();
  testCpm80DriveConfig();
  testCpm80Cpu();
  testCpm80GuestBoot();
  puts("PASS: host utilities and Z80-backed CP/M CPU memory, instruction and port callbacks");
  return 0;
}
