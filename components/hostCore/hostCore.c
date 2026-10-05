#include "hostCore.h"
#include <stdio.h>
#include <string.h>

static bool isLetterOrDigit(unsigned char character)
{
  return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
         (character >= '0' && character <= '9');
}

bool pathIsSafe(const char *path)
{
  size_t length = strlen(path);
  if (length >= relativePathCapacity)
  {
    return false;
  }
  if (length == 0)
  {
    return true;
  }
  size_t segmentLength = 0;
  for (size_t index = 0; index < length; ++index)
  {
    unsigned char character = (unsigned char)path[index];
    if (character == '/')
    {
      if (segmentLength == 0 || path[index - 1] == '.' || path[index - 1] == ' ')
      {
        return false;
      }
      segmentLength = 0;
      continue;
    }
    if (segmentLength == 0 && !isLetterOrDigit(character))
    {
      return false;
    }
    if (!isLetterOrDigit(character) && character != '_' && character != '-' && character != '.' && character != ' ')
    {
      return false;
    }
    if (++segmentLength > 64)
    {
      return false;
    }
  }
  return segmentLength != 0 && path[length - 1] != '.' && path[length - 1] != ' ';
}

static int hexValue(unsigned char character)
{
  if (character >= '0' && character <= '9')
  {
    return character - '0';
  }
  if (character >= 'a' && character <= 'f')
  {
    return character - 'a' + 10;
  }
  if (character >= 'A' && character <= 'F')
  {
    return character - 'A' + 10;
  }
  return -1;
}

bool pathDecode(const char *encoded, char *decoded, size_t capacity)
{
  size_t used = 0;
  for (size_t index = 0; encoded[index] != '\0'; ++index)
  {
    unsigned char character = (unsigned char)encoded[index];
    if (character == '%')
    {
      if (encoded[index + 1] == '\0' || encoded[index + 2] == '\0')
      {
        return false;
      }
      int high = hexValue((unsigned char)encoded[index + 1]);
      int low = hexValue((unsigned char)encoded[index + 2]);
      if (high < 0 || low < 0)
      {
        return false;
      }
      character = (unsigned char)((high << 4) | low);
      index += 2;
    }
    if (character == 0 || used + 1 >= capacity)
    {
      return false;
    }
    decoded[used++] = (char)character;
  }
  if (used >= capacity)
  {
    return false;
  }
  decoded[used] = '\0';
  return pathIsSafe(decoded);
}

bool pathResolve(const char *encoded, char *absolute, size_t capacity)
{
  char relative[relativePathCapacity];
  if (!pathDecode(encoded, relative, sizeof(relative)))
  {
    return false;
  }
  int length = snprintf(absolute, capacity, "/microSD/retro/exchange%s%s", relative[0] ? "/" : "", relative);
  return length >= 0 && (size_t)length < capacity;
}

bool layoutIsValid(const char *data, size_t length)
{
  static const char expected[] = "ESP32-S3-RETRO\nlayout=1\n";
  char normalized[sizeof(expected)];
  size_t used = 0;
  for (size_t index = 0; index < length; ++index)
  {
    char character = data[index];
    if (character == '\r')
    {
      character = '\n';
      if (index + 1 < length && data[index + 1] == '\n')
      {
        ++index;
      }
    }
    if (used >= sizeof(normalized) - 1)
    {
      return false;
    }
    normalized[used++] = character;
  }
  if (used == sizeof(expected) - 2)
  {
    normalized[used++] = '\n';
  }
  return used == sizeof(expected) - 1 && memcmp(normalized, expected, used) == 0;
}

bool menuFeed(menuLine *line, int character, bool *valid)
{
  *valid = false;
  if (character == '\n' && line->skipLf)
  {
    line->skipLf = false;
    return false;
  }
  line->skipLf = false;
  if (character == '\r' || character == '\n')
  {
    line->text[line->length] = '\0';
    *valid = !line->overflow;
    line->length = 0;
    line->overflow = false;
    line->skipLf = character == '\r';
    return true;
  }
  if (character == 8 || character == 127)
  {
    if (line->length > 0)
    {
      --line->length;
    }
    return false;
  }
  if (character < 32 || character > 126)
  {
    line->overflow = true;
    return false;
  }
  if (line->length + 1 >= sizeof(line->text))
  {
    line->overflow = true;
    return false;
  }
  line->text[line->length++] = (char)character;
  return false;
}
