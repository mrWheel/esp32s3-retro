#include "storagePath.h"
#include <stdio.h>
#include <string.h>

static const char *sdMountPath = "/microSD";
static const char *retroPathPrefix = "/retro/";

bool storageResolveRetroPath(const char *retroPath, char *resolvedPath, size_t pathCapacity)
{
  if (retroPath == NULL || resolvedPath == NULL || pathCapacity == 0)
  {
    return false;
  }

  size_t pathLength = strlen(retroPath);
  size_t prefixLength = strlen(retroPathPrefix);
  size_t mountLength = strlen(sdMountPath);
  if (pathLength <= prefixLength || strncmp(retroPath, retroPathPrefix, prefixLength) != 0 ||
      retroPath[pathLength - 1] == '/' || strstr(retroPath, "//") != NULL ||
      strstr(retroPath, "/./") != NULL || strstr(retroPath, "/../") != NULL ||
      (pathLength >= 2 && strcmp(retroPath + pathLength - 2, "/.") == 0) ||
      (pathLength >= 3 && strcmp(retroPath + pathLength - 3, "/..") == 0) ||
      strchr(retroPath, '\\') != NULL || strchr(retroPath, ':') != NULL ||
      pathLength + mountLength >= pathCapacity)
  {
    return false;
  }
  for (const unsigned char *character = (const unsigned char *)retroPath; *character != '\0'; ++character)
  {
    if (*character < 0x20 || *character == 0x7f)
    {
      return false;
    }
  }
  return snprintf(resolvedPath, pathCapacity, "%s%s", sdMountPath, retroPath) < (int)pathCapacity;
}
