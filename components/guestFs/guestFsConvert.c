#include "guestFsConvert.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

enum
{
  textHeaderSize = guestFsConvertTextPageSize,
  textPageSize = guestFsConvertTextPageSize,
  //-- Page content stops one byte short so every page ends in at least one NUL.
  textPageContentMax = textPageSize - 1,
  textLineMax = 2048,
  textDle = 0x10,
  textTabWidth = 8,
  textDleChunk = 200,
  basicProgramStart = 0x0801,
  basicMaxLineNumber = 63999,
  basicMaxSourceLine = 239,
  basicTokenFirst = 0x80
};

void* guestFsConvertAlloc(size_t size)
{
#ifdef ESP_PLATFORM
  void* memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (memory != NULL)
  {
    return memory;
  }
#endif
  return malloc(size);
}

static void setDetail(char* detail, size_t capacity, const char* format, unsigned value)
{
  if (detail != NULL && capacity > 0)
  {
    snprintf(detail, capacity, format, value);
  }
}

static void setDetailText(char* detail, size_t capacity, const char* text)
{
  if (detail != NULL && capacity > 0)
  {
    snprintf(detail, capacity, "%s", text);
  }
}

typedef struct
{
  uint8_t* data;
  size_t length;
  size_t capacity;
} byteBuffer;

static bool bufferAppend(byteBuffer* buffer, const void* data, size_t length)
{
  if (buffer->length + length > buffer->capacity)
  {
    size_t capacity = buffer->capacity == 0 ? 4096 : buffer->capacity;
    while (capacity < buffer->length + length)
    {
      capacity *= 2;
    }
    uint8_t* grown = guestFsConvertAlloc(capacity);
    if (grown == NULL)
    {
      return false;
    }
    if (buffer->length > 0)
    {
      memcpy(grown, buffer->data, buffer->length);
    }
    free(buffer->data);
    buffer->data = grown;
    buffer->capacity = capacity;
  }
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
  return true;
}

//-- ---------------------------------------------------------------------------------------------
//-- UCSD Pascal .TEXT
//-- ---------------------------------------------------------------------------------------------

typedef struct
{
  byteBuffer out;
  uint8_t page[textPageSize];
  size_t pagePosition;
  bool failed;
} textEncoder;

static void textFlushPage(textEncoder* encoder)
{
  memset(encoder->page + encoder->pagePosition, 0, textPageSize - encoder->pagePosition);
  if (!bufferAppend(&encoder->out, encoder->page, textPageSize))
  {
    encoder->failed = true;
  }
  encoder->pagePosition = 0;
}

//-- Returns false when the encoded line cannot fit in one page.
static bool textEmitLine(textEncoder* encoder, const uint8_t* plain, size_t plainLength)
{
  uint8_t encoded[textLineMax + 8];
  size_t length = 0;
  size_t leading = 0;
  while (leading < plainLength && plain[leading] == ' ')
  {
    ++leading;
  }
  size_t remaining = leading;
  while (remaining > 0)
  {
    size_t run = remaining > textDleChunk ? textDleChunk : remaining;
    if (run >= 3)
    {
      encoded[length++] = textDle;
      encoded[length++] = (uint8_t)(run + 32);
    }
    else
    {
      memset(encoded + length, ' ', run);
      length += run;
    }
    remaining -= run;
  }
  memcpy(encoded + length, plain + leading, plainLength - leading);
  length += plainLength - leading;
  encoded[length++] = '\r';
  if (length > textPageContentMax)
  {
    return false;
  }
  if (encoder->pagePosition + length > textPageContentMax)
  {
    textFlushPage(encoder);
  }
  memcpy(encoder->page + encoder->pagePosition, encoded, length);
  encoder->pagePosition += length;
  return !encoder->failed;
}

guestFsStatus guestFsTextEncode(const uint8_t* input, size_t length, uint8_t** out,
                                size_t* outLength, char* detail, size_t detailCapacity)
{
  if (length == 0)
  {
    return guestFsErrEmptyFile;
  }
  textEncoder* encoder = guestFsConvertAlloc(sizeof(*encoder));
  if (encoder == NULL)
  {
    return guestFsErrIo;
  }
  memset(encoder, 0, sizeof(*encoder));
  uint8_t header[textHeaderSize];
  memset(header, 0, sizeof(header));
  bool ok = bufferAppend(&encoder->out, header, sizeof(header));
  uint8_t plain[textLineMax];
  size_t plainLength = 0;
  size_t column = 0;
  unsigned lineNumber = 1;
  bool overflow = false;
  size_t index = 0;
  while (ok && index < length)
  {
    uint8_t character = input[index++];
    bool lineEnd = character == '\n' || character == '\r';
    if (character == '\r' && index < length && input[index] == '\n')
    {
      ++index;
    }
    if (lineEnd)
    {
      if (overflow || !textEmitLine(encoder, plain, plainLength))
      {
        if (!encoder->failed)
        {
          setDetail(detail, detailCapacity, "Line %u is too long for a Pascal text page",
                    lineNumber);
        }
        ok = false;
        break;
      }
      plainLength = 0;
      column = 0;
      overflow = false;
      ++lineNumber;
      continue;
    }
    if (character == '\t')
    {
      size_t spaces = textTabWidth - (column % textTabWidth);
      for (size_t count = 0; count < spaces; ++count)
      {
        if (plainLength < sizeof(plain))
        {
          plain[plainLength++] = ' ';
        }
        else
        {
          overflow = true;
        }
      }
      column += spaces;
      continue;
    }
    if (character < 0x20 || character > 0x7E)
    {
      //-- Control characters are dropped; non-ASCII (e.g. UTF-8) bytes become '?'.
      if (character < 0x20)
      {
        continue;
      }
      character = '?';
    }
    if (plainLength < sizeof(plain))
    {
      plain[plainLength++] = character;
    }
    else
    {
      overflow = true;
    }
    ++column;
  }
  if (ok && (plainLength > 0 || overflow))
  {
    if (overflow || !textEmitLine(encoder, plain, plainLength))
    {
      if (!encoder->failed)
      {
        setDetail(detail, detailCapacity, "Line %u is too long for a Pascal text page", lineNumber);
      }
      ok = false;
    }
  }
  if (ok)
  {
    if (encoder->pagePosition > 0 || encoder->out.length == textHeaderSize)
    {
      textFlushPage(encoder);
    }
    ok = !encoder->failed;
  }
  if (!ok)
  {
    bool memoryFailure = encoder->failed || encoder->out.data == NULL;
    free(encoder->out.data);
    free(encoder);
    return memoryFailure ? guestFsErrIo : guestFsErrInvalidArgument;
  }
  *out = encoder->out.data;
  *outLength = encoder->out.length;
  free(encoder);
  return guestFsOk;
}

void guestFsTextDecoderInit(guestFsTextDecoder* decoder)
{
  decoder->position = 0;
  decoder->dlePending = false;
}

static bool textSendSpaces(guestFsSinkFn sink, void* context, size_t count)
{
  static const char spaces[] = "                                                                ";
  while (count > 0)
  {
    size_t run = count > sizeof(spaces) - 1 ? sizeof(spaces) - 1 : count;
    if (!sink(context, spaces, run))
    {
      return false;
    }
    count -= run;
  }
  return true;
}

bool guestFsTextDecode(guestFsTextDecoder* decoder, const uint8_t* data, size_t length,
                       guestFsSinkFn sink, void* context)
{
  uint8_t out[1024];
  size_t count = 0;
  for (size_t index = 0; index < length; ++index)
  {
    uint8_t value = data[index];
    if (decoder->position < textHeaderSize)
    {
      ++decoder->position;
      continue;
    }
    if (decoder->dlePending)
    {
      decoder->dlePending = false;
      if (count > 0 && !sink(context, out, count))
      {
        return false;
      }
      count = 0;
      if (value > 32 && !textSendSpaces(sink, context, (size_t)value - 32))
      {
        return false;
      }
      continue;
    }
    if (value == 0)
    {
      continue;
    }
    if (value == textDle)
    {
      decoder->dlePending = true;
      continue;
    }
    out[count++] = value == '\r' ? '\n' : value;
    if (count == sizeof(out))
    {
      if (!sink(context, out, count))
      {
        return false;
      }
      count = 0;
    }
  }
  return count == 0 || sink(context, out, count);
}

//-- ---------------------------------------------------------------------------------------------
//-- Applesoft BASIC
//-- ---------------------------------------------------------------------------------------------

typedef struct
{
  const char* spelling;
  uint8_t value;
} basicToken;

//-- Same table (and tie-break order) as tools/apple2/apple2Basic.py.
static const basicToken basicTokens[] = {
    {"HIMEM:", 0xA3},  {"LOMEM:", 0xA4},  {"HCOLOR=", 0x92}, {"SPEED=", 0xA9}, {"SCALE=", 0x99},
    {"COLOR=", 0xA0},  {"NOTRACE", 0x9C}, {"INVERSE", 0x9E}, {"NORMAL", 0x9D}, {"RETURN", 0xB1},
    {"RESTORE", 0xAE}, {"RESUME", 0xA6},  {"RECALL", 0xA7},  {"GOSUB", 0xB0},  {"GOTO", 0xAB},
    {"PRINT", 0xBA},   {"INPUT", 0x84},   {"RIGHT$", 0xE9},  {"LEFT$", 0xE8},  {"MID$", 0xEA},
    {"STR$", 0xE4},    {"CHR$", 0xE7},    {"SCRN(", 0xD7},   {"SPC(", 0xC3},   {"TAB(", 0xC0},
    {"ONERR", 0xA5},   {"IN#", 0x8B},     {"PR#", 0x8A},     {"HGR2", 0x90},   {"HLIN", 0x8E},
    {"VLIN", 0x8F},    {"HPLOT", 0x93},   {"XDRAW", 0x95},   {"TRACE", 0x9B},  {"SHLOAD", 0x9A},
    {"HTAB", 0x96},    {"VTAB", 0xA2},    {"PLOT", 0x8D},    {"DRAW", 0x94},   {"HGR", 0x91},
    {"TEXT", 0x89},    {"HOME", 0x97},    {"END", 0x80},     {"FOR", 0x81},    {"NEXT", 0x82},
    {"DATA", 0x83},    {"DIM", 0x86},     {"READ", 0x87},    {"GR", 0x88},     {"CALL", 0x8C},
    {"POP", 0xA1},     {"LET", 0xAA},     {"RUN", 0xAC},     {"IF", 0xAD},     {"ON", 0xB4},
    {"WAIT", 0xB5},    {"LOAD", 0xB6},    {"SAVE", 0xB7},    {"DEF", 0xB8},    {"POKE", 0xB9},
    {"CONT", 0xBB},    {"LIST", 0xBC},    {"CLEAR", 0xBD},   {"GET", 0xBE},    {"NEW", 0xBF},
    {"TO", 0xC1},      {"FN", 0xC2},      {"THEN", 0xC4},    {"AT", 0xC5},     {"NOT", 0xC6},
    {"STEP", 0xC7},    {"AND", 0xCD},     {"OR", 0xCE},      {"SGN", 0xD2},    {"INT", 0xD3},
    {"ABS", 0xD4},     {"USR", 0xD5},     {"FRE", 0xD6},     {"PDL", 0xD8},    {"POS", 0xD9},
    {"SQR", 0xDA},     {"RND", 0xDB},     {"LOG", 0xDC},     {"EXP", 0xDD},    {"COS", 0xDE},
    {"SIN", 0xDF},     {"TAN", 0xE0},     {"ATN", 0xE1},     {"PEEK", 0xE2},   {"LEN", 0xE3},
    {"VAL", 0xE5},     {"ASC", 0xE6},     {"REM", 0xB2},     {"STOP", 0xB3},   {"+", 0xC8},
    {"-", 0xC9},       {"*", 0xCA},       {"/", 0xCB},       {"^", 0xCC},      {">", 0xCF},
    {"=", 0xD0},       {"<", 0xD1},       {"?", 0xBA},       {"&", 0xAF},      {"DEL", 0x85},
    {"ROT=", 0x98},    {"FLASH", 0x9F},   {"STORE", 0xA8}};
static const size_t basicTokenCount = sizeof(basicTokens) / sizeof(basicTokens[0]);

static const uint8_t tokenRem = 0xB2;
static const uint8_t tokenData = 0x83;

//-- Longest spelling that matches at source[index]; ties go to the first table entry.
static const basicToken* basicMatch(const char* source, size_t length, size_t index)
{
  const basicToken* best = NULL;
  size_t bestLength = 0;
  for (size_t entry = 0; entry < basicTokenCount; ++entry)
  {
    const char* spelling = basicTokens[entry].spelling;
    size_t spellingLength = strlen(spelling);
    if (spellingLength <= bestLength || index + spellingLength > length ||
        strncasecmp(source + index, spelling, spellingLength) != 0)
    {
      continue;
    }
    size_t end = index + spellingLength;
    bool symbolic = !isalnum((unsigned char)spelling[0]) || spelling[spellingLength - 1] == '(' ||
                    spelling[spellingLength - 1] == ':';
    if (symbolic || end == length || !(isalpha((unsigned char)source[end]) || source[end] == '$'))
    {
      best = &basicTokens[entry];
      bestLength = spellingLength;
    }
  }
  return best;
}

static size_t basicTokenizeLine(const char* source, size_t length, uint8_t* out)
{
  size_t count = 0;
  size_t index = 0;
  bool inString = false;
  bool dataMode = false;
  while (index < length)
  {
    char character = source[index];
    if (character == '"')
    {
      inString = !inString;
      out[count++] = (uint8_t)character;
      ++index;
      continue;
    }
    if (inString || dataMode)
    {
      out[count++] = (uint8_t)character;
      if (dataMode && character == ':')
      {
        dataMode = false;
      }
      ++index;
      continue;
    }
    if (isspace((unsigned char)character))
    {
      ++index;
      continue;
    }
    const basicToken* token = basicMatch(source, length, index);
    if (token == NULL)
    {
      out[count++] = (uint8_t)toupper((unsigned char)character);
      ++index;
      continue;
    }
    out[count++] = token->value;
    index += strlen(token->spelling);
    if (token->value == tokenRem)
    {
      memcpy(out + count, source + index, length - index);
      count += length - index;
      break;
    }
    if (token->value == tokenData)
    {
      dataMode = true;
    }
  }
  return count;
}

bool guestFsBasicLooksLikeSource(const uint8_t* data, size_t length)
{
  size_t index = 0;
  for (size_t scan = 0; scan < length; ++scan)
  {
    if (data[scan] == 0)
    {
      return false;
    }
  }
  while (index < length && isspace(data[index]) && data[index] != '\n' && data[index] != '\r')
  {
    ++index;
  }
  return index < length && isdigit(data[index]);
}

typedef struct
{
  uint16_t number;
  uint32_t offset;
  uint8_t length;
} basicLine;

static int basicLineCompare(const void* left, const void* right)
{
  const basicLine* a = left;
  const basicLine* b = right;
  return (int)a->number - (int)b->number;
}

guestFsStatus guestFsBasicTokenize(const uint8_t* input, size_t length, uint8_t** out,
                                   size_t* outLength, char* detail, size_t detailCapacity)
{
  size_t maxLines = 1;
  for (size_t index = 0; index < length; ++index)
  {
    if (input[index] == '\n' || input[index] == '\r')
    {
      ++maxLines;
    }
  }
  basicLine* lines = guestFsConvertAlloc(maxLines * sizeof(*lines));
  uint8_t* store = guestFsConvertAlloc(length + 1);
  if (lines == NULL || store == NULL)
  {
    free(lines);
    free(store);
    return guestFsErrIo;
  }
  size_t lineCount = 0;
  size_t storeLength = 0;
  guestFsStatus status = guestFsOk;
  unsigned sourceLine = 0;
  size_t index = 0;
  while (index < length && status == guestFsOk)
  {
    size_t start = index;
    while (index < length && input[index] != '\n' && input[index] != '\r')
    {
      ++index;
    }
    size_t end = index;
    if (index < length)
    {
      if (input[index] == '\r' && index + 1 < length && input[index + 1] == '\n')
      {
        ++index;
      }
      ++index;
    }
    ++sourceLine;
    size_t cursor = start;
    while (cursor < end && isspace(input[cursor]))
    {
      ++cursor;
    }
    if (cursor == end)
    {
      continue;
    }
    size_t digitsStart = cursor;
    unsigned long number = 0;
    while (cursor < end && isdigit(input[cursor]) && number <= basicMaxLineNumber)
    {
      number = number * 10 + (unsigned long)(input[cursor] - '0');
      ++cursor;
    }
    while (cursor < end && isdigit(input[cursor]))
    {
      ++cursor;
      number = basicMaxLineNumber + 1;
    }
    if (cursor == digitsStart || (cursor < end && !isspace(input[cursor])))
    {
      setDetail(detail, detailCapacity, "Applesoft source line %u must start with a line number",
                sourceLine);
      status = guestFsErrInvalidArgument;
      break;
    }
    if (number > basicMaxLineNumber)
    {
      setDetail(detail, detailCapacity, "Applesoft line number on source line %u is too large",
                sourceLine);
      status = guestFsErrInvalidArgument;
      break;
    }
    while (cursor < end && isspace(input[cursor]))
    {
      ++cursor;
    }
    size_t bodyLength = end - cursor;
    if (bodyLength > basicMaxSourceLine)
    {
      setDetail(detail, detailCapacity, "Applesoft source line %u is too long", sourceLine);
      status = guestFsErrInvalidArgument;
      break;
    }
    for (size_t scan = cursor; scan < end; ++scan)
    {
      if (input[scan] > 0x7E)
      {
        setDetail(detail, detailCapacity, "Applesoft source line %u must contain ASCII only",
                  sourceLine);
        status = guestFsErrInvalidArgument;
        break;
      }
    }
    if (status != guestFsOk)
    {
      break;
    }
    lines[lineCount].number = (uint16_t)number;
    lines[lineCount].offset = (uint32_t)storeLength;
    lines[lineCount].length =
        (uint8_t)basicTokenizeLine((const char*)input + cursor, bodyLength, store + storeLength);
    storeLength += lines[lineCount].length;
    ++lineCount;
  }
  uint8_t* program = NULL;
  if (status == guestFsOk && lineCount == 0)
  {
    setDetailText(detail, detailCapacity, "Applesoft source contains no numbered lines");
    status = guestFsErrInvalidArgument;
  }
  if (status == guestFsOk)
  {
    qsort(lines, lineCount, sizeof(*lines), basicLineCompare);
    size_t total = 2;
    for (size_t entry = 0; entry < lineCount; ++entry)
    {
      if (entry > 0 && lines[entry].number == lines[entry - 1].number)
      {
        setDetail(detail, detailCapacity, "Duplicate Applesoft line number %u",
                  lines[entry].number);
        status = guestFsErrInvalidArgument;
        break;
      }
      total += 5 + lines[entry].length;
    }
    if (status == guestFsOk && basicProgramStart + total > 0xFFFF)
    {
      setDetailText(detail, detailCapacity, "Tokenized program exceeds the Apple II memory");
      status = guestFsErrNoSpace;
    }
    if (status == guestFsOk)
    {
      program = guestFsConvertAlloc(total);
      status = program == NULL ? guestFsErrIo : guestFsOk;
    }
    if (status == guestFsOk)
    {
      size_t position = 0;
      for (size_t entry = 0; entry < lineCount; ++entry)
      {
        size_t next = basicProgramStart + position + 5 + lines[entry].length;
        program[position++] = (uint8_t)(next & 0xFF);
        program[position++] = (uint8_t)(next >> 8);
        program[position++] = (uint8_t)(lines[entry].number & 0xFF);
        program[position++] = (uint8_t)(lines[entry].number >> 8);
        memcpy(program + position, store + lines[entry].offset, lines[entry].length);
        position += lines[entry].length;
        program[position++] = 0;
      }
      program[position++] = 0;
      program[position++] = 0;
      *out = program;
      *outLength = position;
    }
  }
  free(lines);
  free(store);
  return status;
}

static const char* basicTokenName(uint8_t value)
{
  for (size_t entry = 0; entry < basicTokenCount; ++entry)
  {
    if (basicTokens[entry].value == value)
    {
      return basicTokens[entry].spelling;
    }
  }
  return "?";
}

guestFsStatus guestFsBasicDetokenize(const uint8_t* program, size_t length, guestFsSinkFn sink,
                                     void* context, char* detail, size_t detailCapacity)
{
  size_t position = 0;
  char line[16 + 256 * 9];
  while (position + 2 <= length)
  {
    unsigned next = program[position] | (unsigned)(program[position + 1] << 8);
    if (next == 0)
    {
      return guestFsOk;
    }
    if (position + 4 > length)
    {
      break;
    }
    unsigned number = program[position + 2] | (unsigned)(program[position + 3] << 8);
    position += 4;
    size_t used = (size_t)snprintf(line, sizeof(line), "%u ", number);
    bool inString = false;
    bool dataMode = false;
    bool terminated = false;
    bool rest = false;
    while (position < length)
    {
      uint8_t value = program[position++];
      if (value == 0)
      {
        terminated = true;
        break;
      }
      if (used + 12 >= sizeof(line))
      {
        break;
      }
      if (rest || inString || dataMode || value < basicTokenFirst)
      {
        if (value == '"' && !rest)
        {
          inString = !inString;
        }
        if (dataMode && !rest && value == ':')
        {
          dataMode = false;
        }
        line[used++] = (char)value;
        continue;
      }
      const char* name = basicTokenName(value);
      if (line[used - 1] != ' ')
      {
        line[used++] = ' ';
      }
      size_t nameLength = strlen(name);
      memcpy(line + used, name, nameLength);
      used += nameLength;
      //-- REM and DATA keep their original text (and its spacing) right behind the keyword.
      if (value != tokenRem && value != tokenData)
      {
        line[used++] = ' ';
      }
      if (value == tokenRem)
      {
        rest = true;
      }
      else if (value == tokenData)
      {
        dataMode = true;
      }
    }
    if (!terminated)
    {
      break;
    }
    while (used > 0 && line[used - 1] == ' ')
    {
      --used;
    }
    line[used++] = '\n';
    if (!sink(context, line, used))
    {
      return guestFsErrAborted;
    }
  }
  if (position == length)
  {
    return guestFsOk;
  }
  setDetailText(detail, detailCapacity, "The Applesoft program is damaged");
  return guestFsErrCorrupt;
}
