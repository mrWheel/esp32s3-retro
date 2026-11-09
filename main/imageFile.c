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

#include "imageFile.h"
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

bool resourceSize(const char* path, uint64_t* size)
{
  struct stat info;
  if (path == NULL || size == NULL || stat(path, &info) != 0 || !S_ISREG(info.st_mode) ||
      info.st_size < 0)
  {
    return false;
  }
  *size = (uint64_t)info.st_size;
  return true;
}

bool imageOpen(imageFile* image, const char* path, bool readOnly)
{
  if (image == NULL || image->file != NULL || !resourceSize(path, &image->size) ||
      image->size > LONG_MAX)
  {
    return false;
  }
  image->file = fopen(path, readOnly ? "rb" : "r+b");
  image->readOnly = readOnly;
  return image->file != NULL;
}

uint64_t imageSize(const imageFile* image)
{
  return image == NULL || image->file == NULL ? 0 : image->size;
}

static bool seekImage(imageFile* image, uint64_t offset, size_t length)
{
  if (image == NULL || image->file == NULL || offset > image->size ||
      (uint64_t)length > image->size - offset || offset > LONG_MAX)
  {
    return false;
  }
  return fseek(image->file, (long)offset, SEEK_SET) == 0;
}

bool imageReadAt(imageFile* image, uint64_t offset, void* buffer, size_t length)
{
  return buffer != NULL && seekImage(image, offset, length) &&
         fread(buffer, 1, length, image->file) == length;
}

bool imageWriteAt(imageFile* image, uint64_t offset, const void* buffer, size_t length)
{
  return image != NULL && !image->readOnly && buffer != NULL && seekImage(image, offset, length) &&
         fwrite(buffer, 1, length, image->file) == length;
}

bool imageFlush(imageFile* image)
{
  if (image == NULL || image->file == NULL)
  {
    return false;
  }
  if (image->readOnly)
  {
    return true;
  }
  return fflush(image->file) == 0 && fsync(fileno(image->file)) == 0;
}

bool imageClose(imageFile* image)
{
  if (image == NULL || image->file == NULL)
  {
    return false;
  }
  bool success = imageFlush(image);
  if (fclose(image->file) != 0)
  {
    success = false;
  }
  image->file = NULL;
  image->size = 0;
  return success;
}
