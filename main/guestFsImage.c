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
