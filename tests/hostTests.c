#include "hostCore.h"
#include "hostExchange.h"
#include "imageFile.h"
#include "cpm80Cpu.h"
#include "cpm80Guest.h"
#include "cpm80DriveConfig.h"
#include "cpm86Core.h"
#include "cpm86BiosOverlay.h"
#include "cpm86DriveConfig.h"
#include "storagePath.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

static size_t countOccurrences(const char *text, const char *needle);

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

typedef struct
{
  uint8_t input;
  uint8_t output;
} cpm86PortFixture;

static bool cpm86TestPortRead(void *context, uint16_t port, uint8_t *value)
{
  cpm86PortFixture *fixture = context;
  if (port != 0x00F0 || fixture == NULL || value == NULL)
  {
    return false;
  }
  *value = fixture->input;
  return true;
}

static bool cpm86TestPortWrite(void *context, uint16_t port, uint8_t value)
{
  cpm86PortFixture *fixture = context;
  if (port != 0x00F1 || fixture == NULL)
  {
    return false;
  }
  fixture->output = value;
  return true;
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

static void testCpm86Core(void)
{
  char exchangeDirectoryPath[] = "/tmp/cpm86-exchange-test-XXXXXX";
  assert(mkdtemp(exchangeDirectoryPath) != NULL);
  hostExchange exchange = {0};
  assert(hostExchangeInitialize(&exchange, exchangeDirectoryPath));

  cpm86Core *core = NULL;
  cpm86PortFixture portFixture = {.input = 0xA5};
  const cpm86CoreConfig coreConfig = {.ramSize = 128 * 1024,
                                      .portRead = cpm86TestPortRead,
                                      .portWrite = cpm86TestPortWrite,
                                      .portContext = &portFixture};
  assert(cpm86CoreCreate(&core, &exchange, &coreConfig) == cpm86CoreOk);
  cpm86Core *secondCore = NULL;
  assert(cpm86CoreCreate(&secondCore, &exchange, &coreConfig) == cpm86CoreBusy);
  assert(secondCore == NULL);

  const uint8_t arithmeticProgram[] = {
      0xB8, 0x34, 0x12, 0xBB, 0x00, 0x02, 0x05, 0x01, 0x00, 0x89, 0x07, 0xF4};
  assert(cpm86CoreLoad(core, 0, arithmeticProgram, sizeof(arithmeticProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  size_t executed = 0;
  assert(cpm86CoreRun(core, 16, &executed) == cpm86CoreHalted);
  assert(executed == 5);
  assert(cpm86CoreInstructionCount(core) == 5);
  uint8_t arithmeticResult[2];
  assert(cpm86CoreRead(core, 0x0200, arithmeticResult, sizeof(arithmeticResult)) == cpm86CoreOk);
  assert(arithmeticResult[0] == 0x35 && arithmeticResult[1] == 0x12);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t decimalAdjustProgram[] = {0xB0, 0x5A, 0x14, 0x40, 0x27, 0xA2, 0x00, 0x03,
                                          0x9C, 0x58, 0xA3, 0x02, 0x03, 0xF4};
  assert(cpm86CoreLoad(core, 0, decimalAdjustProgram, sizeof(decimalAdjustProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 16, &executed) == cpm86CoreHalted);
  assert(executed == 8);
  uint8_t decimalAdjustResult;
  uint16_t decimalAdjustFlags;
  assert(cpm86CoreRead(core, 0x0300, &decimalAdjustResult, sizeof(decimalAdjustResult)) == cpm86CoreOk);
  assert(cpm86CoreRead(core, 0x0302, &decimalAdjustFlags, sizeof(decimalAdjustFlags)) == cpm86CoreOk);
  assert(decimalAdjustResult == 0x00);
  assert((decimalAdjustFlags & 0x0055) == 0x0055);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t arithmeticFlagsProgram[] = {0xB0, 0x7F, 0x04, 0x01, 0x9C, 0x58, 0xA3, 0x00, 0x03,
                                            0xB0, 0xFF, 0x04, 0x01, 0x9C, 0x58, 0xA3, 0x02, 0x03, 0xF4};
  assert(cpm86CoreLoad(core, 0, arithmeticFlagsProgram, sizeof(arithmeticFlagsProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 16, &executed) == cpm86CoreHalted);
  uint16_t arithmeticFlags[2];
  assert(cpm86CoreRead(core, 0x0300, arithmeticFlags, sizeof(arithmeticFlags)) == cpm86CoreOk);
  assert((arithmeticFlags[0] & 0x08D5) == 0x0890);
  assert((arithmeticFlags[1] & 0x08D5) == 0x0055);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t parityBranchProgram[] = {0xB0, 0x00, 0x04, 0x00, 0xB0, 0x7F, 0x04, 0x01, 0x7A, 0x07,
                                         0xB0, 0x11, 0xA2, 0x00, 0x03, 0xEB, 0x05, 0xB0, 0x22, 0xA2,
                                         0x00, 0x03, 0xF4};
  assert(cpm86CoreLoad(core, 0, parityBranchProgram, sizeof(parityBranchProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 16, &executed) == cpm86CoreHalted);
  uint8_t parityBranchResult;
  assert(cpm86CoreRead(core, 0x0300, &parityBranchResult, sizeof(parityBranchResult)) == cpm86CoreOk);
  assert(parityBranchResult == 0x11);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t pushaProgram[] = {
      0xB8, 0x34, 0x12, 0xB9, 0x78, 0x56, 0xBA, 0xBC, 0x9A, 0xBB, 0xF0, 0xDE, 0xBD, 0x22, 0x11, 0xBE,
      0x44, 0x33, 0xBF, 0x66, 0x55, 0xBC, 0x00, 0x08, 0x60, 0x61, 0xA3, 0x00, 0x04, 0x89, 0x0E, 0x02,
      0x04, 0x89, 0x16, 0x04, 0x04, 0x89, 0x1E, 0x06, 0x04, 0x89, 0x26, 0x08, 0x04, 0x89, 0x2E, 0x0A,
      0x04, 0x89, 0x36, 0x0C, 0x04, 0x89, 0x3E, 0x0E, 0x04, 0xF4};
  assert(cpm86CoreLoad(core, 0, pushaProgram, sizeof(pushaProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 32, &executed) == cpm86CoreHalted);
  uint16_t pushaRegisters[8];
  const uint16_t expectedPushaRegisters[8] = {0x1234, 0x5678, 0x9ABC, 0xDEF0, 0x0800, 0x1122, 0x3344, 0x5566};
  assert(cpm86CoreRead(core, 0x0400, pushaRegisters, sizeof(pushaRegisters)) == cpm86CoreOk);
  assert(memcmp(pushaRegisters, expectedPushaRegisters, sizeof(pushaRegisters)) == 0);
  uint16_t savedStackPointer;
  assert(cpm86CoreRead(core, 0x07F6, &savedStackPointer, sizeof(savedStackPointer)) == cpm86CoreOk);
  assert(savedStackPointer == 0x0800);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t multiplyShiftProgram[] = {
      0xB0, 0x10, 0xB3, 0x10, 0xF6, 0xE3, 0xA3, 0x02, 0x03, 0x9C, 0x58, 0xA3, 0x00, 0x03,
      0xB0, 0x7F, 0xB1, 0x02, 0xF6, 0xE9, 0xA3, 0x06, 0x03, 0x9C, 0x58, 0xA3, 0x04, 0x03,
      0xB0, 0x80, 0xD0, 0xF8, 0xA2, 0x0A, 0x03, 0x9C, 0x58, 0xA3, 0x08, 0x03, 0xB0, 0x80,
      0xD0, 0xE0, 0xA2, 0x0E, 0x03, 0x9C, 0x58, 0xA3, 0x0C, 0x03, 0xF4};
  assert(cpm86CoreLoad(core, 0, multiplyShiftProgram, sizeof(multiplyShiftProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 40, &executed) == cpm86CoreHalted);
  uint8_t multiplyShiftResults[15];
  assert(cpm86CoreRead(core, 0x0300, multiplyShiftResults, sizeof(multiplyShiftResults)) == cpm86CoreOk);
  assert(((multiplyShiftResults[0] | (multiplyShiftResults[1] << 8)) & 0x0801) == 0x0801);
  assert((multiplyShiftResults[2] | (multiplyShiftResults[3] << 8)) == 0x0100);
  assert(((multiplyShiftResults[4] | (multiplyShiftResults[5] << 8)) & 0x0801) == 0x0801);
  assert((multiplyShiftResults[6] | (multiplyShiftResults[7] << 8)) == 0x00FE);
  assert(((multiplyShiftResults[8] | (multiplyShiftResults[9] << 8)) & 0x08D5) == 0x0084);
  assert(multiplyShiftResults[10] == 0xC0);
  assert(((multiplyShiftResults[12] | (multiplyShiftResults[13] << 8)) & 0x08D5) == 0x0845);
  assert(multiplyShiftResults[14] == 0x00);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t rotateProgram[] = {
      0xB0, 0x81, 0xD0, 0xC0, 0xA2, 0x00, 0x03, 0x9C, 0x58, 0xA3, 0x02, 0x03,
      0xB0, 0x01, 0xD0, 0xC8, 0xA2, 0x04, 0x03, 0x9C, 0x58, 0xA3, 0x06, 0x03,
      0xF9, 0xB0, 0x80, 0xD0, 0xD0, 0xA2, 0x08, 0x03, 0x9C, 0x58, 0xA3, 0x0A, 0x03,
      0xF9, 0xB0, 0x01, 0xD0, 0xD8, 0xA2, 0x0C, 0x03, 0x9C, 0x58, 0xA3, 0x0E, 0x03, 0xF4};
  assert(cpm86CoreLoad(core, 0, rotateProgram, sizeof(rotateProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 32, &executed) == cpm86CoreHalted);
  uint8_t rotateResults[16];
  assert(cpm86CoreRead(core, 0x0300, rotateResults, sizeof(rotateResults)) == cpm86CoreOk);
  const uint8_t expectedRotateResults[] = {0x03, 0x80, 0x01, 0x80};
  for (size_t index = 0; index < sizeof(expectedRotateResults); ++index)
  {
    assert(rotateResults[index * 4] == expectedRotateResults[index]);
    uint16_t flags = rotateResults[index * 4 + 2] | (rotateResults[index * 4 + 3] << 8);
    assert((flags & 0x0801) == 0x0801);
  }

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t signedDivisionProgram[] = {
      0xB8, 0xFA, 0xFF, 0xB3, 0xFE, 0xF6, 0xFB, 0xA3, 0x00, 0x03, 0xB8, 0xFA, 0xFF, 0xBA, 0xFF, 0xFF,
      0xBB, 0xFE, 0xFF, 0xF7, 0xFB, 0xA3, 0x02, 0x03, 0x89, 0x16, 0x04, 0x03, 0xF4};
  assert(cpm86CoreLoad(core, 0, signedDivisionProgram, sizeof(signedDivisionProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 24, &executed) == cpm86CoreHalted);
  uint16_t signedDivisionResults[3];
  assert(cpm86CoreRead(core, 0x0300, signedDivisionResults, sizeof(signedDivisionResults)) == cpm86CoreOk);
  assert(signedDivisionResults[0] == 3);
  assert(signedDivisionResults[1] == 3);
  assert(signedDivisionResults[2] == 0);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t wrapProgram[] = {0xB8, 0xFF, 0xFF, 0x8E, 0xD8, 0xBB, 0x0F, 0x01, 0x8B, 0x07,
                                 0x50, 0xB8, 0x00, 0x00, 0x8E, 0xD8, 0x58, 0xA3, 0x00, 0x03, 0xF4};
  const uint8_t wrappedValue[2] = {0x78, 0x56};
  assert(cpm86CoreWrite(core, 0x00FF, wrappedValue, sizeof(wrappedValue)) == cpm86CoreOk);
  assert(cpm86CoreLoad(core, 0, wrapProgram, sizeof(wrapProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 24, &executed) == cpm86CoreHalted);
  uint8_t wrappedResult[2];
  assert(cpm86CoreRead(core, 0x0300, wrappedResult, sizeof(wrappedResult)) == cpm86CoreOk);
  assert(wrappedResult[0] == 0x78 && wrappedResult[1] == 0x56);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t queryProgram[] = {0xB0, 0x00, 0xE6, 0xF8, 0xE4, 0xF8, 0xA2, 0x00, 0x03,
                                  0xE4, 0xF8, 0xA2, 0x01, 0x03, 0xE4, 0xF8, 0xA2, 0x02, 0x03, 0xF4};
  assert(cpm86CoreLoad(core, 0, queryProgram, sizeof(queryProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 16, &executed) == cpm86CoreHalted);
  assert(executed == 9);
  uint8_t queryResult[3];
  assert(cpm86CoreRead(core, 0x0300, queryResult, sizeof(queryResult)) == cpm86CoreOk);
  assert(queryResult[0] == hostExchangeStatusOk);
  assert(queryResult[1] == 1);
  assert(queryResult[2] == 0x07);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t wordIoProgram[] = {0xB0, 0x00, 0xE6, 0xF8, 0xE4, 0xF8, 0xE5, 0xF8, 0xE4, 0xF8,
                                   0xA2, 0x00, 0x03, 0xF4};
  assert(cpm86CoreLoad(core, 0, wordIoProgram, sizeof(wordIoProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreStep(core) == cpm86CoreOk);
  assert(cpm86CoreStep(core) == cpm86CoreOk);
  assert(cpm86CoreStep(core) == cpm86CoreOk);
  assert(cpm86CoreStep(core) == cpm86CoreIoError);
  assert(cpm86CoreStep(core) == cpm86CoreOk);
  assert(cpm86CoreStep(core) == cpm86CoreOk);
  uint8_t version = 0;
  assert(cpm86CoreRead(core, 0x0300, &version, sizeof(version)) == cpm86CoreOk);
  assert(version == 1);

  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t targetPortProgram[] = {0xE4, 0xF0, 0xA2, 0x00, 0x03, 0xB0, 0x5A, 0xE6, 0xF1, 0xF4};
  assert(cpm86CoreLoad(core, 0, targetPortProgram, sizeof(targetPortProgram)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0, 0) == cpm86CoreOk);
  assert(cpm86CoreRun(core, 8, &executed) == cpm86CoreHalted);
  uint8_t targetPortResult = 0;
  assert(cpm86CoreRead(core, 0x0300, &targetPortResult, sizeof(targetPortResult)) == cpm86CoreOk);
  assert(targetPortResult == 0xA5);
  assert(portFixture.output == 0x5A);

  const uint8_t byte = 0;
  assert(cpm86CoreLoad(core, coreConfig.ramSize, &byte, sizeof(byte)) == cpm86CoreMemoryRange);
  assert(cpm86CoreReset(core) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0xF000, 0) == cpm86CoreOk);
  assert(cpm86CoreStep(core) == cpm86CoreMemoryFault);
  assert(cpm86CoreReset(core) == cpm86CoreOk);
  const uint8_t invalidOpcode = 0x0F;
  assert(cpm86CoreLoad(core, 0x20, &invalidOpcode, sizeof(invalidOpcode)) == cpm86CoreOk);
  assert(cpm86CoreSetEntry(core, 0x0000, 0x0020) == cpm86CoreOk);
  assert(cpm86CoreStep(core) == cpm86CoreInvalidInstruction);
  uint16_t programCounterSegment;
  uint16_t programCounterOffset;
  assert(cpm86CoreGetProgramCounter(core, &programCounterSegment, &programCounterOffset) == cpm86CoreOk);
  assert(programCounterSegment == 0 && programCounterOffset == 0x0020);
  cpm86CoreTraceEntry trace[cpm86CoreTraceDepth];
  size_t traceCount = 0;
  assert(cpm86CoreGetRecentTrace(core, trace, cpm86CoreTraceDepth, &traceCount) == cpm86CoreOk);
  assert(traceCount == 1 && trace[0].segment == 0 && trace[0].offset == 0x0020);

  cpm86CoreDestroy(core);
  hostExchangeClose(&exchange);
  assert(rmdir(exchangeDirectoryPath) == 0);
}

typedef struct
{
  imageFile *disks[cpm86DiskDriveCount];
  bool largeDisks[cpm86DiskDriveCount];
  uint8_t record[128];
  uint16_t track;
  uint16_t sector;
  uint8_t drive;
  size_t transferIndex;
  uint8_t status;
  bool reading;
  bool writing;
  size_t diskReadRequests;
  size_t diskReadFailures;
  size_t diskRecordsTransferred;
  const char *input;
  size_t inputLength;
  size_t inputPosition;
  char output[32768];
  size_t outputLength;
} cpm86BootFixture;

static bool cpm86BootPortRead(void *context, uint16_t port, uint8_t *value)
{
  cpm86BootFixture *fixture = context;
  if (fixture == NULL || value == NULL)
  {
    return false;
  }
  if (port == 0x00E0)
  {
    *value = fixture->inputPosition < fixture->inputLength ? 0xFF : 0;
    return true;
  }
  if (port == 0x00E1 && fixture->inputPosition < fixture->inputLength)
  {
    *value = (uint8_t)fixture->input[fixture->inputPosition++];
    return true;
  }
  if (port == 0x00ED)
  {
    *value = fixture->status;
    return true;
  }
  if (port == 0x00EF)
  {
    bool available = fixture->drive < cpm86DiskDriveCount && fixture->disks[fixture->drive] != NULL;
    *value = !available ? 0 : fixture->largeDisks[fixture->drive] ? 0x01 : 0xFF;
    return true;
  }
  if (port == 0x00EE && fixture->reading && fixture->transferIndex < sizeof(fixture->record))
  {
    *value = fixture->record[fixture->transferIndex++];
    if (fixture->transferIndex == sizeof(fixture->record))
    {
      ++fixture->diskRecordsTransferred;
    }
    return true;
  }
  return false;
}

static bool cpm86BootDiskOffset(cpm86BootFixture *fixture, uint64_t *offset)
{
  if (fixture->drive >= cpm86DiskDriveCount || fixture->disks[fixture->drive] == NULL ||
      offset == NULL || fixture->track == 0 ||
      fixture->track >= (fixture->largeDisks[fixture->drive] ? 129 : 40) || fixture->sector >= 32)
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)fixture->track * 32 + fixture->sector;
  *offset = recordIndex * sizeof(fixture->record);
  imageFile *disk = fixture->disks[fixture->drive];
  return *offset <= imageSize(disk) && sizeof(fixture->record) <= imageSize(disk) - *offset;
}

static bool cpm86BootPortWrite(void *context, uint16_t port, uint8_t value)
{
  cpm86BootFixture *fixture = context;
  if (fixture == NULL)
  {
    return false;
  }
  if (port == 0x00E2)
  {
    if (fixture->outputLength + 1 >= sizeof(fixture->output))
    {
      return false;
    }
    fixture->output[fixture->outputLength++] = (char)value;
    fixture->output[fixture->outputLength] = '\0';
    return true;
  }
  if (port == 0x00E8)
  {
    fixture->drive = value;
    return true;
  }
  if (port == 0x00E9)
  {
    fixture->track = (fixture->track & 0xFF00) | value;
    return true;
  }
  if (port == 0x00EA)
  {
    fixture->track = (fixture->track & 0x00FF) | ((uint16_t)value << 8);
    return true;
  }
  if (port == 0x00EB)
  {
    fixture->sector = (fixture->sector & 0xFF00) | value;
    return true;
  }
  if (port == 0x00EC)
  {
    fixture->sector = (fixture->sector & 0x00FF) | ((uint16_t)value << 8);
    return true;
  }
  if (port == 0x00ED)
  {
    uint64_t offset;
    fixture->status = 1;
    fixture->reading = false;
    fixture->writing = false;
    fixture->transferIndex = 0;
    if (!cpm86BootDiskOffset(fixture, &offset))
    {
      if (value == 0)
      {
        ++fixture->diskReadRequests;
        ++fixture->diskReadFailures;
      }
      return true;
    }
    if (value == 0)
    {
      ++fixture->diskReadRequests;
      if (imageReadAt(fixture->disks[fixture->drive], offset, fixture->record, sizeof(fixture->record)))
      {
        fixture->reading = true;
        fixture->status = 0;
      }
      else
      {
        ++fixture->diskReadFailures;
      }
    }
    else if (value == 1)
    {
      fixture->writing = true;
      fixture->status = 0;
    }
    return true;
  }
  if (port == 0x00EE && fixture->writing && fixture->transferIndex < sizeof(fixture->record))
  {
    fixture->record[fixture->transferIndex++] = value;
    if (fixture->transferIndex == sizeof(fixture->record))
    {
      uint64_t offset;
      if (!cpm86BootDiskOffset(fixture, &offset) ||
          !imageWriteAt(fixture->disks[fixture->drive], offset, fixture->record, sizeof(fixture->record)) ||
          !imageFlush(fixture->disks[fixture->drive]))
      {
        fixture->status = 1;
      }
      fixture->writing = false;
    }
    return true;
  }
  return false;
}

static bool runCpm86UntilPrompt(cpm86Core *core, cpm86BootFixture *fixture, const char *prompt,
                                size_t promptCount)
{
  size_t instructions = 0;
  size_t instructionBudget = getenv("CPM86_HOST_BUILD_DISK") == NULL ? 5000000 : 100000000;
  while (countOccurrences(fixture->output, prompt) < promptCount && instructions < instructionBudget)
  {
    size_t executed = 0;
    cpm86CoreResult result = cpm86CoreRun(core, 4096, &executed);
    instructions += executed;
    if (result != cpm86CoreOk)
    {
      uint16_t segment;
      uint16_t offset;
      cpm86CoreTraceEntry trace[cpm86CoreTraceDepth];
      size_t traceCount = 0;
      uint8_t codeBytes[8];
      assert(cpm86CoreGetProgramCounter(core, &segment, &offset) == cpm86CoreOk);
      assert(cpm86CoreGetRecentTrace(core, trace, cpm86CoreTraceDepth, &traceCount) == cpm86CoreOk);
      uint32_t physicalAddress = (((uint32_t)segment << 4) + offset) & 0x000FFFFF;
      assert(cpm86CoreRead(core, physicalAddress, codeBytes, sizeof(codeBytes)) == cpm86CoreOk);
      fprintf(stderr, "CP/M-86 run failed: result=%d count=%llu output=%s\n",
              result, (unsigned long long)cpm86CoreInstructionCount(core), fixture->output);
      fprintf(stderr,
              "CP/M-86 failure at %04X:%04X bytes=%02X %02X %02X %02X %02X %02X %02X %02X; "
              "recent instructions:\n",
              segment, offset, codeBytes[0], codeBytes[1], codeBytes[2], codeBytes[3], codeBytes[4],
              codeBytes[5], codeBytes[6], codeBytes[7]);
      for (size_t index = 0; index < traceCount; ++index)
      {
        fprintf(stderr, "  %04X:%04X SS:SP=%04X:%04X %s\n", trace[index].segment,
                trace[index].offset, trace[index].stackSegment, trace[index].stackPointer,
                trace[index].instruction);
      }
      return false;
    }
  }
  bool reachedPrompt = countOccurrences(fixture->output, prompt) == promptCount;
  if (!reachedPrompt)
  {
    fprintf(stderr,
            "CP/M-86 prompt timeout: prompt=%s wanted=%zu found=%zu instructions=%zu reads=%zu readFailures=%zu records=%zu output=%s\n",
            prompt, promptCount, countOccurrences(fixture->output, prompt), instructions,
            fixture->diskReadRequests, fixture->diskReadFailures, fixture->diskRecordsTransferred,
            fixture->output);
  }
  return reachedPrompt;
}

//-- Builds a RETRO86_DATA_LARGE_V1 image the way tools/diskImageCpm.py does: 2 KiB blocks, one
//-- 16 KiB (128 record) directory entry per extent, extent numbers 0,1,2,... and 8 block pointers per entry.
static void writeCpm86LargeTextImage(const char *path, size_t lineCount)
{
  enum
  {
    imageBytes = 528384,
    reservedBytes = 4096,
    blockBytes = 2048,
    directoryBlocks = 2,
    recordBytes = 128
  };
  uint8_t *image = malloc(imageBytes);
  assert(image != NULL);
  memset(image, 0xE5, imageBytes);

  size_t fileBytes = lineCount * 7;
  size_t recordCount = (fileBytes + recordBytes - 1) / recordBytes;
  size_t blockCount = (recordCount * recordBytes + blockBytes - 1) / blockBytes;
  uint8_t *data = image + reservedBytes + directoryBlocks * blockBytes;
  memset(data, 0x1A, blockCount * blockBytes);
  for (size_t line = 0; line < lineCount; ++line)
  {
    char text[8];
    snprintf(text, sizeof(text), "L%04u\r\n", (unsigned)(line + 1));
    memcpy(data + line * 7, text, 7);
  }

  size_t recordsLeft = recordCount;
  size_t nextBlock = directoryBlocks;
  for (size_t extent = 0; recordsLeft > 0; ++extent)
  {
    uint8_t *entry = image + reservedBytes + extent * 32;
    size_t extentRecords = recordsLeft > 128 ? 128 : recordsLeft;
    memset(entry, 0, 32);
    memcpy(entry + 1, "BIG     TXT", 11);
    entry[12] = (uint8_t)extent;
    entry[15] = (uint8_t)extentRecords;
    for (size_t pointer = 0; pointer < (extentRecords * recordBytes + blockBytes - 1) / blockBytes; ++pointer)
    {
      entry[16 + pointer] = (uint8_t)nextBlock++;
    }
    recordsLeft -= extentRecords;
  }
  assert(nextBlock == directoryBlocks + blockCount);

  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  assert(fwrite(image, 1, imageBytes, file) == imageBytes);
  assert(fclose(file) == 0);
  free(image);
}

static void testCpm86Boot(void)
{
  FILE *systemFile = fopen(CPM86_SYSTEM_FILE_PATH, "rb");
  assert(systemFile != NULL);
  uint8_t systemImage[10240];
  assert(fread(systemImage, 1, sizeof(systemImage), systemFile) == sizeof(systemImage));
  assert(fclose(systemFile) == 0);
  assert(systemImage[0] == 1 && systemImage[3] == 0x51 && systemImage[4] == 0);

  char temporaryDiskPath[] = "/tmp/cpm86-boot-disk-XXXXXX";
  int temporaryDiskDescriptor = mkstemp(temporaryDiskPath);
  assert(temporaryDiskDescriptor >= 0);
  close(temporaryDiskDescriptor);
  const char *hostSystemDiskPath = getenv("CPM86_HOST_SYSTEM_DISK");
  const char *sourceDiskPath = hostSystemDiskPath == NULL ? CPM86_SYSTEM_DISK_PATH : hostSystemDiskPath;
  FILE *sourceDisk = fopen(sourceDiskPath, "rb");
  FILE *temporaryDisk = fopen(temporaryDiskPath, "wb");
  assert(sourceDisk != NULL && temporaryDisk != NULL);
  uint8_t diskCopyBuffer[512];
  size_t bytesCopied = 0;
  size_t bytesRead;
  while ((bytesRead = fread(diskCopyBuffer, 1, sizeof(diskCopyBuffer), sourceDisk)) > 0)
  {
    assert(fwrite(diskCopyBuffer, 1, bytesRead, temporaryDisk) == bytesRead);
    bytesCopied += bytesRead;
  }
  assert(!ferror(sourceDisk));
  assert(fclose(sourceDisk) == 0);
  assert(fclose(temporaryDisk) == 0);
  assert(bytesCopied == 163840);

  imageFile disk = {0};
  assert(imageOpen(&disk, temporaryDiskPath, false));
  cpm86BootFixture fixture = {.disks = {[0] = &disk, [1] = &disk}};
  const char *hostBuildDiskPath = getenv("CPM86_HOST_BUILD_DISK");
  assert((hostBuildDiskPath == NULL) == (hostSystemDiskPath == NULL));
  imageFile hostBuildDisk = {0};
  hostExchange exchange = {0};
  char exchangeDirectoryPath[] = "/tmp/cpm86-host-exchange-XXXXXX";
  char exchangeSourcePath[sizeof(exchangeDirectoryPath) + sizeof("/HELLO.A86")];
  char exchangeOtherPath[sizeof(exchangeDirectoryPath) + sizeof("/OTHER.A86")];
  char exchangeHostPath[sizeof(exchangeDirectoryPath) + sizeof("/HOST.A86")];
  static const uint8_t otherSource[] = {0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF, 0x0D, 0x0A, 0x1A};
  if (hostBuildDiskPath != NULL)
  {
    assert(imageOpen(&hostBuildDisk, hostBuildDiskPath, false));
    assert(imageSize(&hostBuildDisk) == 528384);
    fixture.disks[4] = &hostBuildDisk;
    fixture.largeDisks[4] = true;

    assert(mkdtemp(exchangeDirectoryPath) != NULL);
    assert(hostExchangeInitialize(&exchange, exchangeDirectoryPath));
    snprintf(exchangeSourcePath, sizeof(exchangeSourcePath), "%s/HELLO.A86", exchangeDirectoryPath);
    snprintf(exchangeOtherPath, sizeof(exchangeOtherPath), "%s/OTHER.A86", exchangeDirectoryPath);
    snprintf(exchangeHostPath, sizeof(exchangeHostPath), "%s/HOST.A86", exchangeDirectoryPath);
    uint8_t exchangeSource[43 * 128];
    for (size_t index = 0; index < sizeof(exchangeSource); ++index)
    {
      exchangeSource[index] = (uint8_t)(index * 37U + (index >> 3));
    }
    FILE *exchangeSourceFile = fopen(exchangeSourcePath, "wb");
    assert(exchangeSourceFile != NULL);
    assert(fwrite(exchangeSource, 1, sizeof(exchangeSource), exchangeSourceFile) ==
           sizeof(exchangeSource));
    assert(fclose(exchangeSourceFile) == 0);
    FILE *exchangeOtherFile = fopen(exchangeOtherPath, "wb");
    assert(exchangeOtherFile != NULL);
    assert(fwrite(otherSource, 1, sizeof(otherSource), exchangeOtherFile) == sizeof(otherSource));
    assert(fclose(exchangeOtherFile) == 0);
  }
  const cpm86CoreConfig config = {
      .ramSize = 640 * 1024,
      .portRead = cpm86BootPortRead,
      .portWrite = cpm86BootPortWrite,
      .portContext = &fixture,
  };
  cpm86Core *core = NULL;
  assert(cpm86CoreCreate(&core, hostBuildDiskPath == NULL ? NULL : &exchange, &config) ==
         cpm86CoreOk);
  size_t payloadOffset = 0;
  const size_t payloadSize = sizeof(systemImage) - 128;
  while (payloadOffset < payloadSize)
  {
    size_t chunkSize = payloadSize - payloadOffset;
    if (chunkSize > 512)
    {
      chunkSize = 512;
    }
    assert(cpm86CoreLoad(core, 0x00510 + (uint32_t)payloadOffset, systemImage + 128 + payloadOffset,
                         chunkSize) == cpm86CoreOk);
    payloadOffset += chunkSize;
  }
  const uint8_t bdosVector[] = {0x06, 0x0B, 0x51, 0x00};
  assert(cpm86CoreWrite(core, 0x00380, bdosVector, sizeof(bdosVector)) == cpm86CoreOk);
  assert(cpm86BiosLoadOverlay(core, CPM86_BIOS_OVERLAY_PATH));
  assert(cpm86CoreSetEntry(core, 0x0051, 0x2500) == cpm86CoreOk);

  assert(runCpm86UntilPrompt(core, &fixture, "A>", 1));
  size_t idleInstructions = 0;
  cpm86CoreResult idleResult = cpm86CoreRun(core, 10000, &idleInstructions);
  if (idleResult != cpm86CoreOk)
  {
    uint16_t segment;
    uint16_t offset;
    assert(cpm86CoreGetProgramCounter(core, &segment, &offset) == cpm86CoreOk);
    fprintf(stderr, "CP/M-86 idle console failed: result=%d CS:IP=%04X:%04X executed=%zu\n",
            idleResult, segment, offset, idleInstructions);
  }
  assert(idleResult == cpm86CoreOk);
  fixture.input = "DIR\r";
  fixture.inputLength = 4;
  assert(runCpm86UntilPrompt(core, &fixture, "A>", 2));
  assert(strstr(fixture.output, "ASM86") != NULL);
  assert(strstr(fixture.output, "PIP") != NULL);
  assert(fixture.diskReadRequests > 0);
  assert(fixture.diskReadFailures == 0);
  assert(fixture.diskRecordsTransferred == fixture.diskReadRequests);

  fixture.input = "B:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  assert(runCpm86UntilPrompt(core, &fixture, "B>", 1));
  size_t bDriveReads = fixture.diskReadRequests;
  fixture.input = "DIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  assert(runCpm86UntilPrompt(core, &fixture, "B>", 2));
  assert(fixture.diskReadRequests > bDriveReads);
  assert(strstr(fixture.output, "B:") != NULL);
  assert(strstr(fixture.output, "ASM86") != NULL);
  assert(fixture.diskReadFailures == 0);

  char largeDiskPath[] = "/tmp/cpm86-large-disk-XXXXXX";
  int largeDescriptor = mkstemp(largeDiskPath);
  assert(largeDescriptor >= 0);
  uint8_t formatted[4096];
  memset(formatted, 0xE5, sizeof(formatted));
  for (size_t track = 0; track < 129; ++track)
  {
    assert(write(largeDescriptor, formatted, sizeof(formatted)) == (ssize_t)sizeof(formatted));
  }
  close(largeDescriptor);
  imageFile largeDisk = {0};
  assert(imageOpen(&largeDisk, largeDiskPath, false));
  assert(imageSize(&largeDisk) == 528384);
  fixture.disks[2] = &largeDisk;
  fixture.largeDisks[2] = true;

  fixture.input = "PIP C:=A:PIP.CMD\rPIP C:ASM.CMD=A:ASM86.CMD\rC:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  assert(runCpm86UntilPrompt(core, &fixture, "C>", 1));
  fixture.input = "DIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  assert(runCpm86UntilPrompt(core, &fixture, "C>", 2));
  assert(strstr(fixture.output, "PIP") != NULL);
  assert(strstr(fixture.output, "ASM") != NULL);
  assert(fixture.diskReadFailures == 0);
  assert(imageClose(&largeDisk));
  assert(unlink(largeDiskPath) == 0);

  //-- A text file of 19,600 bytes spans two directory extents (one 16 KiB extent per entry); the BDOS must
  //-- find records 128 and up on a LARGE drive, so every line has to appear in the TYPE output.
  const size_t textLineCount = 2800;
  char textDiskPath[] = "/tmp/cpm86-large-text-XXXXXX";
  int textDescriptor = mkstemp(textDiskPath);
  assert(textDescriptor >= 0);
  close(textDescriptor);
  writeCpm86LargeTextImage(textDiskPath, textLineCount);
  imageFile textDisk = {0};
  assert(imageOpen(&textDisk, textDiskPath, true));
  fixture.disks[3] = &textDisk;
  fixture.largeDisks[3] = true;
  fixture.input = "D:\rTYPE BIG.TXT\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  assert(runCpm86UntilPrompt(core, &fixture, "D>", 2));
  const char *typed = fixture.output;
  for (size_t line = 1; line <= textLineCount; ++line)
  {
    char expected[8];
    snprintf(expected, sizeof(expected), "L%04u\r\n", (unsigned)line);
    typed = strstr(typed, expected);
    if (typed == NULL)
    {
      fprintf(stderr, "CP/M-86 LARGE multi-extent TYPE lost line %zu of %zu\n", line, textLineCount);
    }
    assert(typed != NULL);
    typed += 7;
  }
  assert(fixture.diskReadFailures == 0);
  assert(imageClose(&textDisk));
  assert(unlink(textDiskPath) == 0);

  if (hostBuildDiskPath != NULL)
  {
    fixture.input = "E:\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 1));

    fixture.input = "A:ASM86 E:HOST.A86\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 2));
    if (strstr(fixture.output, "NUMBER OF ERRORS:   0") == NULL)
    {
      fprintf(stderr, "CP/M-86 ASM86 failed:\n%s\n", fixture.output);
    }
    assert(strstr(fixture.output, "NUMBER OF ERRORS:   0") != NULL);

    fixture.input = "A:GENCMD E:HOST\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 3));

    if (getenv("CPM86_HOST_COMPILE_ONLY") != NULL)
    {
      assert(imageClose(&hostBuildDisk));
      hostExchangeClose(&exchange);
      assert(unlink(exchangeSourcePath) == 0);
      assert(unlink(exchangeOtherPath) == 0);
      assert(rmdir(exchangeDirectoryPath) == 0);
      cpm86CoreDestroy(core);
      assert(imageClose(&disk));
      assert(unlink(temporaryDiskPath) == 0);
      return;
    }

    fixture.input = "A:HOST DIR\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 4));
    if (strstr(fixture.output, "HELLO.A86") == NULL)
    {
      fprintf(stderr, "CP/M-86 HOST DIR output:\n%s\n", fixture.output);
    }
    assert(strstr(fixture.output, "HELLO.A86") != NULL);

    fixture.input = "A:HOST GET HELLO.A86\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 5));
    if (strstr(fixture.output, "GET complete.") == NULL)
    {
      fprintf(stderr, "CP/M-86 HOST GET output:\n%s\n", fixture.output);
    }
    assert(strstr(fixture.output, "GET complete.") != NULL);
    assert(unlink(exchangeSourcePath) == 0);

    fixture.input = "A:HOST PUT HELLO.A86\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 6));
    if (strstr(fixture.output, "PUT complete.") == NULL)
    {
      fprintf(stderr, "CP/M-86 HOST PUT output:\n%s\n", fixture.output);
    }
    assert(strstr(fixture.output, "PUT complete.") != NULL);

    uint8_t actualResult[43 * 128];
    FILE *exchangeResultFile = fopen(exchangeSourcePath, "rb");
    assert(exchangeResultFile != NULL);
    assert(fread(actualResult, 1, sizeof(actualResult), exchangeResultFile) == sizeof(actualResult));
    assert(fgetc(exchangeResultFile) == EOF);
    assert(fclose(exchangeResultFile) == 0);
    for (size_t index = 0; index < sizeof(actualResult); ++index)
    {
      assert(actualResult[index] == (uint8_t)(index * 37U + (index >> 3)));
    }

    fixture.input = "A:HOST GET *.A86\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 7));
    if (strstr(fixture.output, "GET HELLO.A86: target file already exists.") == NULL ||
        strstr(fixture.output, "GET OTHER.A86: complete.") == NULL)
    {
      fprintf(stderr, "CP/M-86 HOST wildcard GET output:\n%s\n", fixture.output);
    }
    assert(strstr(fixture.output, "GET HELLO.A86: target file already exists.") != NULL);
    assert(strstr(fixture.output, "GET OTHER.A86: complete.") != NULL);

    fixture.input = "A:HOST GET H?LLO.A86\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 8));
    assert(strstr(fixture.output, "GET HELLO.A86: target file already exists.") != NULL);
    assert(unlink(exchangeOtherPath) == 0);
    FILE *exchangeHostFile = fopen(exchangeHostPath, "wb");
    assert(exchangeHostFile != NULL);
    assert(fputc(0xA5, exchangeHostFile) == 0xA5);
    assert(fclose(exchangeHostFile) == 0);

    fixture.input = "A:HOST PUT *.A86\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 9));
    if (strstr(fixture.output, "PUT HELLO.A86: exchange destination already exists.") == NULL ||
        strstr(fixture.output, "PUT OTHER.A86: complete.") == NULL ||
        strstr(fixture.output, "PUT HOST.A86: exchange destination already exists.") == NULL ||
        countOccurrences(fixture.output, "PUT HOST.A86:") != 1)
    {
      fprintf(stderr, "CP/M-86 HOST wildcard PUT output:\n%s\n", fixture.output);
    }
    assert(strstr(fixture.output, "PUT HELLO.A86: exchange destination already exists.") != NULL);
    assert(strstr(fixture.output, "PUT OTHER.A86: complete.") != NULL);
    assert(strstr(fixture.output, "PUT HOST.A86: exchange destination already exists.") != NULL);
    assert(countOccurrences(fixture.output, "PUT HOST.A86:") == 1);

    FILE *exchangeOtherResultFile = fopen(exchangeOtherPath, "rb");
    assert(exchangeOtherResultFile != NULL);
    uint8_t actualOtherResult[sizeof(otherSource)];
    assert(fread(actualOtherResult, 1, sizeof(actualOtherResult), exchangeOtherResultFile) ==
           sizeof(actualOtherResult));
    assert(fgetc(exchangeOtherResultFile) == EOF);
    assert(fclose(exchangeOtherResultFile) == 0);
    assert(memcmp(actualOtherResult, otherSource, sizeof(otherSource)) == 0);

    fixture.input = "A:HOST PUT H?LLO.A86\r";
    fixture.inputLength = strlen(fixture.input);
    fixture.inputPosition = 0;
    assert(runCpm86UntilPrompt(core, &fixture, "E>", 10));
    assert(strstr(fixture.output, "PUT HELLO.A86: exchange destination already exists.") != NULL);

    assert(imageClose(&hostBuildDisk));
    hostExchangeClose(&exchange);
    assert(unlink(exchangeSourcePath) == 0);
    assert(unlink(exchangeOtherPath) == 0);
    assert(unlink(exchangeHostPath) == 0);
    assert(rmdir(exchangeDirectoryPath) == 0);
  }

  cpm86CoreDestroy(core);
  assert(imageClose(&disk));
  assert(unlink(temporaryDiskPath) == 0);
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

static void testCpm86DriveConfig(void)
{
  char resolvedPath[cpm86DrivePathCapacity];
  assert(storageResolveRetroPath("/retro/images/cpm86/work86.dsk", resolvedPath, sizeof(resolvedPath)));
  assert(strcmp(resolvedPath, "/microSD/retro/images/cpm86/work86.dsk") == 0);
  assert(!storageResolveRetroPath("/retro/images/cpm86/../escape.dsk", resolvedPath, sizeof(resolvedPath)));

  static const char configText[] =
      "A=/littlefs/cpm86/system.dsk,RO,RETRO86_SYSTEM_V1\r\n"
      "B=/retro/images/cpm86/languages.dsk,RO,RETRO86_DATA_V1\n"
      "C=/retro/images/cpm86/tools.dsk,RO,RETRO86_DATA_V1\n"
      "D=/retro/images/cpm86/utilities.dsk,RO,RETRO86_DATA_V1\n"
      "E=/retro/images/cpm86/work86.dsk,RW,RETRO86_DATA_V1\n"
      "F=/retro/images/cpm86/archive.dsk,RW,RETRO86_DATA_LARGE_V1\n";
  char configPath[] = "/tmp/cpm86-drives-config-test-XXXXXX";
  int descriptor = mkstemp(configPath);
  assert(descriptor >= 0);
  FILE *file = fdopen(descriptor, "wb");
  assert(file != NULL);
  assert(fwrite(configText, 1, sizeof(configText) - 1, file) == sizeof(configText) - 1);
  assert(fclose(file) == 0);

  cpm86DriveConfig drives[cpm86DiskDriveCount];
  char error[96];
  assert(cpm86DriveConfigLoad(configPath, drives, error, sizeof(error)) == cpm86DriveConfigLoaded);
  assert(drives[0].configured && drives[0].profile == cpm86DiskProfileSystem && drives[0].readOnly);
  assert(strcmp(drives[1].path, "/microSD/retro/images/cpm86/languages.dsk") == 0);
  assert(drives[1].configured && drives[1].profile == cpm86DiskProfileData && drives[1].readOnly);
  assert(strcmp(drives[4].path, "/microSD/retro/images/cpm86/work86.dsk") == 0);
  assert(drives[4].configured && drives[4].profile == cpm86DiskProfileData && !drives[4].readOnly);
  assert(strcmp(drives[5].path, "/microSD/retro/images/cpm86/archive.dsk") == 0);
  assert(drives[5].configured && drives[5].profile == cpm86DiskProfileDataLarge && !drives[5].readOnly);

  static const char invalidConfigs[][128] = {
      "A=/littlefs/cpm86/system.dsk,RO,RETRO86_DATA_LARGE_V1\n",
      "B=/retro/images/cpm86/../escape.dsk,RW,RETRO86_DATA_V1\n",
      "A=/littlefs/cpm86/system.dsk,RW,RETRO86_SYSTEM_V1\n",
      "B=/retro/images/cpm86/work.dsk,RW,RETRO86_SYSTEM_V1\n",
      "G=/retro/images/cpm86/work.dsk,RW,RETRO86_DATA_V1\n",
      "B=/retro/images/cpm86/work.dsk,RW,RETRO86_DATA_V1\nB=/retro/images/cpm86/work2.dsk,RW,RETRO86_DATA_V1\n",
  };
  for (size_t index = 0; index < sizeof(invalidConfigs) / sizeof(invalidConfigs[0]); ++index)
  {
    file = fopen(configPath, "wb");
    assert(file != NULL);
    assert(fwrite(invalidConfigs[index], 1, strlen(invalidConfigs[index]), file) ==
           strlen(invalidConfigs[index]));
    assert(fclose(file) == 0);
    assert(cpm86DriveConfigLoad(configPath, drives, error, sizeof(error)) == cpm86DriveConfigInvalid);
    assert(drives[0].configured && drives[0].profile == cpm86DiskProfileSystem && drives[0].readOnly);
    for (uint8_t drive = 1; drive < cpm86DiskDriveCount; ++drive)
    {
      assert(!drives[drive].configured);
    }
  }
  assert(unlink(configPath) == 0);

  assert(cpm86DriveConfigLoad(configPath, drives, error, sizeof(error)) == cpm86DriveConfigMissing);
  assert(drives[0].configured && drives[0].profile == cpm86DiskProfileSystem && drives[0].readOnly);
  for (uint8_t drive = 1; drive < cpm86DiskDriveCount; ++drive)
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
  char exchangeOtherFilePath[absolutePathCapacity];
  assert(snprintf(exchangeOtherFilePath, sizeof(exchangeOtherFilePath), "%s/OTHER.BIN", exchangeDirectoryPath) <
         (int)sizeof(exchangeOtherFilePath));
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
  uint8_t exchangeOtherFileBytes[257];
  for (size_t index = 0; index < sizeof(exchangeOtherFileBytes); ++index)
  {
    exchangeOtherFileBytes[index] = (uint8_t)(index * 19U + 5U);
  }
  exchangeFile = fopen(exchangeOtherFilePath, "wb");
  assert(exchangeFile != NULL);
  assert(fwrite(exchangeOtherFileBytes, 1, sizeof(exchangeOtherFileBytes), exchangeFile) ==
         sizeof(exchangeOtherFileBytes));
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
  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:HOST GET INPUT.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 10000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 10000000);
  assert(strstr(fixture.output, "GET complete.") != NULL);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "E:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 && instructions < 11000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 11000000);

  instructions = 0;
  assert(guest.selectedDrive == 4);
  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:HOST GET *.*\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 && instructions < 3000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 3000000);
  if (strstr(fixture.output, "GET INPUT.BIN: target file already exists.") == NULL ||
      strstr(fixture.output, "GET OTHER.BIN: complete.") == NULL)
  {
    fprintf(stderr, "CP/M-80 HOST wildcard GET output:\n%s\n", fixture.output);
  }
  assert(strstr(fixture.output, "GET INPUT.BIN: target file already exists.") != NULL);
  assert(strstr(fixture.output, "GET OTHER.BIN: complete.") != NULL);
  assert(strstr(fixture.output, "GET complete.") != NULL);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:HOST GET INP?T.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 && instructions < 5000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 5000000);
  assert(strstr(fixture.output, "GET INPUT.BIN: target file already exists.") != NULL);

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

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "DIR\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 && instructions < 12000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 12000000);
  assert(strstr(fixture.output, "OTHER    BIN") != NULL);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:PIP E:INPUT2.HST=E:INPUT.HST\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
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

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:HOST GET INPUT2.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 15000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 15000000);
  assert(strstr(fixture.output, "GET INPUT2.BIN: sidecar metadata file already exists.") != NULL);

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

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:HOST GET INPUT.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 18000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 18000000);
  assert(strstr(fixture.output, "GET INPUT.BIN: target file already exists.") != NULL);
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

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:HOST PUT INPUT.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 &&
         instructions < 22000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 22000000);
  if (strstr(fixture.output, "PUT complete.") == NULL)
  {
    fprintf(stderr, "CP/M-80 HOST PUT output:\n%s\n", fixture.output);
  }
  assert(strstr(fixture.output, "PUT complete.") != NULL);
  exchangeFile = fopen(exchangeFilePath, "rb");
  assert(exchangeFile != NULL);
  uint8_t roundTripBytes[sizeof(exchangeFileBytes)];
  assert(fread(roundTripBytes, 1, sizeof(roundTripBytes), exchangeFile) == sizeof(roundTripBytes));
  assert(fgetc(exchangeFile) == EOF);
  assert(fclose(exchangeFile) == 0);
  assert(memcmp(roundTripBytes, exchangeFileBytes, sizeof(roundTripBytes)) == 0);
  instructions = 0;

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "E:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 && instructions < 2000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 2000000);
  assert(guest.selectedDrive == 4);

  assert(unlink(exchangeOtherFilePath) == 0);
  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:HOST PUT *.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 && instructions < 7000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 7000000);
  if (strstr(fixture.output, "PUT INPUT.BIN: exchange destination already exists.") == NULL ||
      strstr(fixture.output, "PUT OTHER.BIN: complete.") == NULL)
  {
    fprintf(stderr, "CP/M-80 HOST wildcard PUT output:\n%s\n", fixture.output);
  }
  assert(strstr(fixture.output, "PUT INPUT.BIN: exchange destination already exists.") != NULL);
  assert(strstr(fixture.output, "PUT OTHER.BIN: complete.") != NULL);
  assert(strstr(fixture.output, "PUT complete.") != NULL);
  exchangeFile = fopen(exchangeOtherFilePath, "rb");
  assert(exchangeFile != NULL);
  uint8_t wildcardPutBytes[sizeof(exchangeOtherFileBytes)];
  assert(fread(wildcardPutBytes, 1, sizeof(wildcardPutBytes), exchangeFile) == sizeof(wildcardPutBytes));
  assert(fgetc(exchangeFile) == EOF);
  assert(fclose(exchangeFile) == 0);
  assert(memcmp(wildcardPutBytes, exchangeOtherFileBytes, sizeof(wildcardPutBytes)) == 0);
  instructions = 0;

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "E:\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 && instructions < 2000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 2000000);
  assert(guest.selectedDrive == 4);

  priorWorkPrompts = countOccurrences(fixture.output, "E>");
  fixture.input = "A:HOST PUT INP?T.BIN\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "E>") < priorWorkPrompts + 1 && instructions < 4000000)
  {
    size_t executed = cpm80GuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 4000000);
  assert(strstr(fixture.output, "PUT INPUT.BIN: exchange destination already exists.") != NULL);

  instructions = 0;
  priorSystemPrompts = countOccurrences(fixture.output, "A>");
  fixture.input = "A:\r";
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
  fixture.input = "A:\rPIP E:=A:HELLO.COM\r";
  fixture.inputLength = strlen(fixture.input);
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < priorSystemPrompts + 2 && instructions < 12000000)
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
  assert(unlink(exchangeOtherFilePath) == 0);
  assert(rmdir(exchangeDirectoryPath) == 0);
}

int main(void)
{
  testPaths();
  testHostExchange();
  testCpm86Core();
  testCpm86Boot();
  testLayout();
  testMenu();
  testImages();
  testCpm80DriveConfig();
  testCpm86DriveConfig();
  testCpm80Cpu();
  testCpm80GuestBoot();
  puts("PASS: host utilities, Z80 and 8086 CPU fixtures, CP/M-86 boot and DIR");
  return 0;
}
