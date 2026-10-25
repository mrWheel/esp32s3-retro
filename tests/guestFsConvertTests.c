#include "guestFsConvert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define check(condition)                                                                           \
  do                                                                                               \
  {                                                                                                \
    if (!(condition))                                                                              \
    {                                                                                              \
      fprintf(stderr, "CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition);                 \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)

typedef struct
{
  char data[8192];
  size_t length;
} collector;

static bool collect(void* context, const void* data, size_t length)
{
  collector* target = context;
  check(target->length + length < sizeof(target->data));
  memcpy(target->data + target->length, data, length);
  target->length += length;
  return true;
}

static void testTextRoundTrip(void)
{
  const char* source = "PROGRAM T;\r\nBEGIN\r\n    WRITELN('HI');\r\n\tX := 1;\r\n\r\nEND.";
  uint8_t* encoded = NULL;
  size_t encodedLength = 0;
  char detail[96] = "";
  check(guestFsTextEncode((const uint8_t*)source, strlen(source), &encoded, &encodedLength, detail,
                          sizeof(detail)) == guestFsOk);
  check(encodedLength == 2048);
  for (size_t index = 0; index < 1024; ++index)
  {
    check(encoded[index] == 0);
  }
  static const char page[] = "PROGRAM T;\rBEGIN\r\x10\x24WRITELN('HI');\r\x10\x28X := 1;\r\rEND.\r";
  check(memcmp(encoded + 1024, page, sizeof(page) - 1) == 0);
  check(encoded[1024 + sizeof(page) - 1] == 0);
  check(encoded[2047] == 0);
  guestFsTextDecoder decoder;
  guestFsTextDecoderInit(&decoder);
  collector result = {{0}, 0};
  check(guestFsTextDecode(&decoder, encoded, 700, collect, &result));
  check(guestFsTextDecode(&decoder, encoded + 700, encodedLength - 700, collect, &result));
  const char* expected = "PROGRAM T;\nBEGIN\n    WRITELN('HI');\n        X := 1;\n\nEND.\n";
  check(result.length == strlen(expected));
  check(memcmp(result.data, expected, result.length) == 0);
  free(encoded);
}

static void testTextPaging(void)
{
  char source[5000];
  size_t length = 0;
  for (int line = 0; line < 100; ++line)
  {
    length +=
        (size_t)snprintf(source + length, sizeof(source) - length, "LINE %03d ........\n", line);
  }
  uint8_t* encoded = NULL;
  size_t encodedLength = 0;
  check(guestFsTextEncode((const uint8_t*)source, length, &encoded, &encodedLength, NULL, 0) ==
        guestFsOk);
  check(encodedLength % 1024 == 0 && encodedLength >= 3072);
  for (size_t page = 1; page < encodedLength / 1024; ++page)
  {
    check(encoded[page * 1024 + 1023] == 0);
  }
  guestFsTextDecoder decoder;
  guestFsTextDecoderInit(&decoder);
  collector result = {{0}, 0};
  check(guestFsTextDecode(&decoder, encoded, encodedLength, collect, &result));
  check(result.length == length && memcmp(result.data, source, length) == 0);
  free(encoded);
  uint8_t* none = NULL;
  size_t noneLength = 0;
  check(guestFsTextEncode((const uint8_t*)"", 0, &none, &noneLength, NULL, 0) ==
        guestFsErrEmptyFile);
  char longLine[1100];
  memset(longLine, 'A', sizeof(longLine));
  check(guestFsTextEncode((const uint8_t*)longLine, sizeof(longLine), &none, &noneLength, NULL,
                          0) == guestFsErrInvalidArgument);
}

static void testBasic(void)
{
  const char* source = "20 PRINT \"A\":GOTO 10\r\n10 REM Hello  there\n30 for i=1 to 3: next i\n"
                       "40 DATA a, b : END\n";
  check(guestFsBasicLooksLikeSource((const uint8_t*)source, strlen(source)));
  uint8_t* program = NULL;
  size_t programLength = 0;
  char detail[96] = "";
  check(guestFsBasicTokenize((const uint8_t*)source, strlen(source), &program, &programLength,
                             detail, sizeof(detail)) == guestFsOk);
  static const uint8_t expectedStart[] = {0x14, 0x08, 0x0A, 0x00, 0xB2, ' ', 'H', 'e', 'l', 'l',
                                          'o',  ' ',  ' ',  't',  'h',  'e', 'r', 'e', 0x00};
  check(programLength > sizeof(expectedStart));
  check(memcmp(program, expectedStart, sizeof(expectedStart)) == 0);
  check(program[programLength - 1] == 0 && program[programLength - 2] == 0);
  check(!guestFsBasicLooksLikeSource(program, programLength));
  collector result = {{0}, 0};
  check(guestFsBasicDetokenize(program, programLength, collect, &result, detail, sizeof(detail)) ==
        guestFsOk);
  result.data[result.length] = '\0';
  const char* expected = "10 REM Hello  there\n20 PRINT \"A\": GOTO 10\n30 FOR I = 1 TO 3: NEXT I\n"
                         "40 DATA a, b : END\n";
  check(strcmp(result.data, expected) == 0);
  uint8_t* again = NULL;
  size_t againLength = 0;
  check(guestFsBasicTokenize((const uint8_t*)result.data, result.length, &again, &againLength,
                             detail, sizeof(detail)) == guestFsOk);
  check(againLength == programLength && memcmp(again, program, programLength) == 0);
  free(again);
  free(program);
  check(guestFsBasicTokenize((const uint8_t*)"10 END\n10 END\n", 14, &program, &programLength,
                             detail, sizeof(detail)) == guestFsErrInvalidArgument);
  check(guestFsBasicTokenize((const uint8_t*)"PRINT 1\n", 8, &program, &programLength, detail,
                             sizeof(detail)) == guestFsErrInvalidArgument);
}

int main(void)
{
  testTextRoundTrip();
  testTextPaging();
  testBasic();
  printf("guestFsConvert tests passed\n");
  return 0;
}
