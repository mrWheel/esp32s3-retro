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

#include "guestFsImage.h"

static bool guestImageReadAt(void* context, uint64_t offset, void* buffer, size_t length)
{
  return imageReadAt(context, offset, buffer, length);
}

static bool guestImageWriteAt(void* context, uint64_t offset, const void* buffer, size_t length)
{
  return imageWriteAt(context, offset, buffer, length);
}

static bool guestImageFlush(void* context)
{
  return imageFlush(context);
}

void guestFsImageBind(guestFsIo* io, imageFile* image)
{
  io->context = image;
  io->readAt = guestImageReadAt;
  io->writeAt = guestImageWriteAt;
  io->flush = guestImageFlush;
  io->size = imageSize(image);
  io->readOnly = image->readOnly;
}
