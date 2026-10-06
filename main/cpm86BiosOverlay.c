#include "cpm86BiosOverlay.h"
#include "cpm86Core.h"
#include "imageFile.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum
{
  biosSegmentBase = 0x510,
  biosOverlayStart = 0x24B7,
  biosOverlayEnd = 0x286D,
  biosOverlayCapacity = biosOverlayEnd - biosOverlayStart,
  biosOverlayFileLimit = 4096
};

static int hexDigit(char character)
{
  if (character >= '0' && character <= '9')
  {
    return character - '0';
  }
  if (character >= 'A' && character <= 'F')
  {
    return character - 'A' + 10;
  }
  if (character >= 'a' && character <= 'f')
  {
    return character - 'a' + 10;
  }
  return -1;
}

static bool decodeByte(const char *text, uint8_t *value)
{
  int high = hexDigit(text[0]);
  int low = hexDigit(text[1]);
  if (high < 0 || low < 0 || value == NULL)
  {
    return false;
  }
  *value = (uint8_t)((high << 4) | low);
  return true;
}

static bool applyRecord(const char *line, uint8_t overlay[biosOverlayCapacity],
                        bool written[biosOverlayCapacity], bool *hasData, bool *hasStart,
                        bool *hasEnd)
{
  if (line[0] != ':')
  {
    return false;
  }

  uint8_t bytes[260];
  uint8_t byteCount;
  if (!decodeByte(line + 1, &byteCount))
  {
    return false;
  }
  size_t recordByteCount = (size_t)byteCount + 5;
  size_t expectedLength = 1 + recordByteCount * 2;
  if (strlen(line) != expectedLength)
  {
    return false;
  }
  for (size_t index = 0; index < recordByteCount; ++index)
  {
    if (!decodeByte(line + 1 + index * 2, &bytes[index]))
    {
      return false;
    }
  }

  uint8_t checksum = 0;
  for (size_t index = 0; index < recordByteCount; ++index)
  {
    checksum = (uint8_t)(checksum + bytes[index]);
  }
  if (checksum != 0)
  {
    return false;
  }

  uint16_t address = (uint16_t)((uint16_t)bytes[1] << 8) | bytes[2];
  uint8_t recordType = bytes[3];
  if (recordType == 0x81)
  {
    if (byteCount == 0 || address < biosOverlayStart ||
        (size_t)(address - biosOverlayStart) + byteCount > biosOverlayCapacity)
    {
      return false;
    }
    size_t targetOffset = address - biosOverlayStart;
    for (size_t index = 0; index < byteCount; ++index)
    {
      if (written[targetOffset + index])
      {
        return false;
      }
      overlay[targetOffset + index] = bytes[4 + index];
      written[targetOffset + index] = true;
    }
    *hasData = true;
    return true;
  }
  if (recordType == 0x03 && byteCount == 4 && !*hasStart)
  {
    *hasStart = true;
    return true;
  }
  if (recordType == 0x01 && byteCount == 0 && !*hasEnd)
  {
    *hasEnd = true;
    return true;
  }
  return false;
}

bool cpm86BiosLoadOverlay(cpm86Core *core, const char *path)
{
  if (core == NULL || path == NULL)
  {
    return false;
  }

  imageFile image = {0};
  if (!imageOpen(&image, path, true))
  {
    return false;
  }
  if (imageSize(&image) == 0 || imageSize(&image) > biosOverlayFileLimit)
  {
    imageClose(&image);
    return false;
  }

  uint8_t overlay[biosOverlayCapacity] = {0};
  bool written[biosOverlayCapacity] = {false};
  bool hasData = false;
  bool hasStart = false;
  bool hasEnd = false;
  bool valid = true;
  bool sawEnd = false;
  char line[600];
  while (fgets(line, sizeof(line), image.file) != NULL)
  {
    size_t length = strlen(line);
    if (length == sizeof(line) - 1 && line[length - 1] != '\n' && !feof(image.file))
    {
      valid = false;
      break;
    }
    bool cpMEndOfFile = false;
    char *endMarker = memchr(line, 0x1A, length);
    if (endMarker != NULL)
    {
      length = (size_t)(endMarker - line);
      cpMEndOfFile = true;
    }
    while (length > 0 && (line[length - 1] == '\r' || line[length - 1] == '\n'))
    {
      line[--length] = '\0';
    }
    if (length == 0)
    {
      if (cpMEndOfFile)
      {
        valid = hasEnd;
        sawEnd = true;
        break;
      }
      continue;
    }
    if (sawEnd || !applyRecord(line, overlay, written, &hasData, &hasStart, &hasEnd))
    {
      valid = false;
      break;
    }
    sawEnd = hasEnd;
    if (cpMEndOfFile)
    {
      break;
    }
  }
  valid = valid && !ferror(image.file) && hasData && hasStart && hasEnd;
  if (!imageClose(&image))
  {
    valid = false;
  }
  if (!valid)
  {
    return false;
  }

  size_t index = 0;
  while (index < biosOverlayCapacity)
  {
    while (index < biosOverlayCapacity && !written[index])
    {
      ++index;
    }
    size_t start = index;
    while (index < biosOverlayCapacity && written[index])
    {
      ++index;
    }
    if (start < index &&
        cpm86CoreLoad(core, biosSegmentBase + biosOverlayStart + (uint32_t)start,
                      overlay + start, index - start) != cpm86CoreOk)
    {
      return false;
    }
  }
  return true;
}
