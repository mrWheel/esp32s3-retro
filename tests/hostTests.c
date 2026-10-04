#include "hostCore.h"
#include "imageFile.h"
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

int main(void)
{
  testPaths();
  testLayout();
  testMenu();
  testImages();
  puts("PASS: traversal, 50000 path fuzz cases, layout variants, ENTER parsing, bounded binary image I/O");
  return 0;
}
