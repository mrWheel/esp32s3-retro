/*
 * Copyright (C) 2026 Willem Aandewiel
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "storagePath.h"
#include <stdio.h>
#include <string.h>

static const char* sdMountPath = "/microSD";
static const char* retroPathPrefix = "/retro/";

bool storageResolveRetroPath(const char* retroPath, char* resolvedPath, size_t pathCapacity)
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
  for (const unsigned char* character = (const unsigned char*)retroPath; *character != '\0';
       ++character)
  {
    if (*character < 0x20 || *character == 0x7f)
    {
      return false;
    }
  }
  return snprintf(resolvedPath, pathCapacity, "%s%s", sdMountPath, retroPath) < (int)pathCapacity;
}
