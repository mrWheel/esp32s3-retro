#include "hostCore.h"
#include "imageFile.h"
#include "cpmCpu.h"
#include "cpmGuest.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void testPaths(void)
{
  const char *rejected[] = {"../images/cpm/disk",
                            "%2e%2e/images",
                            "cpm/%2E%2E/x",
                            "/etc/passwd",
                            "%2fetc/passwd",
                            "cpm//x",
                            "cpm/",
                            "cpm/./x",
                            "cpm/%00.txt",
                            "cpm/x%00tail",
                            "%252e%252e/x",
                            "%",
                            "%0",
                            "%xy",
                            "cpm\\x",
                            "cpm%5cx",
                            "cpm/a:",
                            "cpm/foo.",
                            "cpm/foo%20",
                            ".upload-part",
                            "cpm/a%0ab",
                            "cpm/a%22b",
                            "cpm/%3Cscript%3E",
                            "cpm/%7fx",
                            "cpm/a+b",
                            "cpm/x?y",
                            "cpm/x#y"};
  char path[absolutePathCapacity];
  for (size_t index = 0; index < sizeof(rejected) / sizeof(rejected[0]); ++index)
  {
    assert(!pathResolve(rejected[index], path, sizeof(path)));
  }
  assert(pathResolve("", path, sizeof(path)));
  assert(strcmp(path, "/sdcard/retro/exchange") == 0);
  assert(pathResolve("common/Hello%20World.bin", path, sizeof(path)));
  assert(strcmp(path, "/sdcard/retro/exchange/common/Hello World.bin") == 0);
  assert(pathResolve("cpm%2Fa.bin", path, sizeof(path)));
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
      assert(strncmp(path, "/sdcard/retro/exchange", 22) == 0);
      assert(strstr(path, "/../") == NULL);
      assert(strlen(path) < sizeof(path));
    }
  }
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

static void testCpmCpu(void)
{
  cpmCpu cpu = {0};
  portFixture ports = {.inputValue = 0xA5};
  const uint8_t program[] = {0x3E, 0x5A, 0x32, 0xFF, 0xFF, 0xDB, 0x42, 0xD3, 0x43, 0x76};
  uint8_t value;

  assert(!cpmCpuInitialize(NULL, testPortInput, testPortOutput, &ports));
  assert(!cpmCpuInitialize(&cpu, NULL, testPortOutput, &ports));
  assert(!cpmCpuInitialize(&cpu, testPortInput, NULL, &ports));
  assert(cpmCpuInitialize(&cpu, testPortInput, testPortOutput, &ports));
  assert(cpmCpuLoad(&cpu, 0, program, sizeof(program)));
  assert(!cpmCpuLoad(&cpu, UINT16_MAX, program, 2));
  assert(!cpmCpuLoad(&cpu, 0, NULL, 1));
  assert(cpmCpuWriteMemory(&cpu, UINT16_MAX, 0xC3));
  assert(cpmCpuReadMemory(&cpu, UINT16_MAX, &value) && value == 0xC3);
  assert(!cpmCpuReadMemory(&cpu, 0, NULL));

  assert(cpmCpuStep(&cpu));
  assert(cpmCpuStep(&cpu));
  assert(cpmCpuReadMemory(&cpu, UINT16_MAX, &value) && value == 0x5A);
  assert(cpmCpuStep(&cpu));
  assert(cpu.processor.a == 0xA5 && ports.inputPort == 0x42);
  assert(cpmCpuStep(&cpu));
  assert(ports.outputPort == 0x43 && ports.outputValue == 0xA5);
  assert(cpmCpuStep(&cpu) && cpu.processor.halted);
  assert(cpu.processor.pc == sizeof(program));
  assert(!cpmCpuStep(NULL));
  cpmCpuDestroy(&cpu);
  assert(!cpu.initialized && cpu.memory == NULL);
}

typedef struct
{
  imageFile *disk;
  const char *input;
  size_t inputLength;
  size_t inputPosition;
  char output[8192];
  size_t outputLength;
  bool outputOverflow;
} cpmGuestFixture;

static bool cpmTestConsoleAvailable(void *context)
{
  cpmGuestFixture *fixture = context;
  return fixture->inputPosition < fixture->inputLength;
}

static int cpmTestConsoleRead(void *context)
{
  cpmGuestFixture *fixture = context;
  return fixture->inputPosition < fixture->inputLength ? (unsigned char)fixture->input[fixture->inputPosition++] : -1;
}

static void cpmTestConsoleWrite(void *context, uint8_t character)
{
  cpmGuestFixture *fixture = context;
  if (fixture->outputLength + 1 >= sizeof(fixture->output))
  {
    fixture->outputOverflow = true;
    return;
  }
  fixture->output[fixture->outputLength++] = (char)character;
  fixture->output[fixture->outputLength] = '\0';
}

static bool cpmTestDiskRead(void *context, uint8_t drive, uint16_t track, uint16_t sector,
                            uint8_t record[cpmDiskSectorSize])
{
  cpmGuestFixture *fixture = context;
  if (drive != 0 || track >= cpmDiskTracks || sector >= cpmDiskSectorsPerTrack)
  {
    return false;
  }
  uint64_t recordIndex = (uint64_t)track * cpmDiskSectorsPerTrack + sector;
  return imageReadAt(fixture->disk, recordIndex * cpmDiskSectorSize, record, cpmDiskSectorSize);
}

static void cpmTestYield(void *context)
{
  (void)context;
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

static void testCpmGuestBoot(void)
{
  imageFile disk = {0};
  assert(imageOpen(&disk, CPM_SYSTEM_IMAGE_PATH, true));
  assert(imageSize(&disk) == cpmSystemImageSize);
  uint8_t ccpImage[cpmCcpSize];
  uint8_t bdosImage[cpmBdosSize];
  assert(imageReadAt(&disk, 0, ccpImage, sizeof(ccpImage)));
  assert(imageReadAt(&disk, sizeof(ccpImage), bdosImage, sizeof(bdosImage)));

  cpmGuestFixture fixture = {.disk = &disk};
  const cpmHostOps host = {.consoleAvailable = cpmTestConsoleAvailable,
                           .consoleRead = cpmTestConsoleRead,
                           .consoleWrite = cpmTestConsoleWrite,
                           .diskReadRecord = cpmTestDiskRead,
                           .yield = cpmTestYield,
                           .context = &fixture};
  cpmGuest guest = {0};
  assert(cpmGuestInitialize(&guest, &host, ccpImage, bdosImage));
  assert(cpmGuestColdBoot(&guest));
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

  size_t instructions = 0;
  while (countOccurrences(fixture.output, "A>") < 1 && instructions < 500000)
  {
    size_t executed = cpmGuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(countOccurrences(fixture.output, "A>") == 1);

  fixture.input = "DIR\r";
  fixture.inputLength = 4;
  fixture.inputPosition = 0;
  while (countOccurrences(fixture.output, "A>") < 2 && instructions < 2000000)
  {
    size_t executed = cpmGuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(countOccurrences(fixture.output, "A>") == 2);
  assert(strstr(fixture.output, "HELLO    COM") != NULL);

  fixture.input = "HELLO\r";
  fixture.inputLength = 6;
  fixture.inputPosition = 0;
  while ((countOccurrences(fixture.output, "A>") < 3 ||
          strstr(fixture.output, "HELLO FROM CP/M 2.2") == NULL) &&
         instructions < 5000000)
  {
    size_t executed = cpmGuestRunFor(&guest, 10000);
    assert(executed > 0);
    instructions += executed;
  }
  assert(instructions < 5000000);
  assert(strstr(fixture.output, "HELLO FROM CP/M 2.2") != NULL);
  assert(countOccurrences(fixture.output, "A>") >= 3);
  assert(!fixture.outputOverflow);

  const uint8_t invalidDmaProgram[] = {0x0E, 0x01, 0xCD, 0x1B, 0xDA, 0x0E, 0x00, 0xCD, 0x1B, 0xDA,
                                      0x01, 0xC1, 0xFF, 0xCD, 0x24, 0xDA, 0xCD, 0x27, 0xDA, 0x76};
  assert(cpmCpuLoad(&guest.cpu, 0x0100, invalidDmaProgram, sizeof(invalidDmaProgram)));
  guest.cpu.processor.pc = 0x0100;
  guest.cpu.processor.sp = 0x0200;
  while (!guest.cpu.processor.halted)
  {
    assert(cpmGuestStep(&guest));
  }
  assert(guest.selectedDrive == 0);
  assert(guest.cpu.processor.a == 1);
  assert(guest.cpu.processor.h == 0xDA && guest.cpu.processor.l == 0x90);

  cpmGuestDestroy(&guest);
  assert(imageClose(&disk));
}

int main(void)
{
  testPaths();
  testLayout();
  testMenu();
  testImages();
  testCpmCpu();
  testCpmGuestBoot();
  puts("PASS: host utilities and Z80-backed CP/M CPU memory, instruction and port callbacks");
  return 0;
}
