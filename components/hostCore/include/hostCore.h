#pragma once
#include <stdbool.h>
#include <stddef.h>

enum
{
  relativePathCapacity = 256,
  absolutePathCapacity = 320,
  menuLineCapacity = 32
};

typedef struct
{
  char text[menuLineCapacity];
  size_t length;
  bool overflow;
  bool skipLf;
} menuLine;

bool pathIsSafe(const char *path);
bool pathDecode(const char *encoded, char *decoded, size_t capacity);
bool pathResolve(const char *encoded, char *absolute, size_t capacity);
bool layoutIsValid(const char *data, size_t length);
bool menuFeed(menuLine *line, int character, bool *valid);
