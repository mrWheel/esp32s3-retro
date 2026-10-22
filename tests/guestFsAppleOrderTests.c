#include "apple2DiskImage.h"
#include "guestFs.h"
#include "guestFsImage.h"
#include "guestFsInternal.h"
#include "imageFile.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int checkedImages = 0;
static int failures = 0;

static void checkImage(const char* directory, const char* osId, const char* fileName)
{
  char path[1024];
  char imageId[256];
  char detail[guestFsDetailCapacity];
  snprintf(path, sizeof(path), "%s/%s", directory, fileName);
  snprintf(imageId, sizeof(imageId), "%s/%s", osId, fileName);

  apple2DiskImage reference;
  apple2DiskImageInitialize(&reference);
  struct stat fileInfo;
  if (stat(path, &fileInfo) != 0)
  {
    return;
  }
  apple2DiskImageProfile profile =
      fileInfo.st_size == apple2DiskImage800kSize   ? apple2DiskImageProfile800k
      : fileInfo.st_size == apple2DiskImage640kSize ? apple2DiskImageProfile640k
                                                    : apple2DiskImageProfile140k;
  if (apple2DiskImageOpenProfile(&reference, path, profile, apple2DiskImageOrderAuto) !=
      apple2DiskImageOk)
  {
    return;
  }
  bool referenceDos = reference.order == apple2DiskImageOrderDos;
  apple2DiskImageClose(&reference);
  imageFile image;
  memset(&image, 0, sizeof(image));
  if (!imageOpen(&image, path, true))
  {
    printf("FAIL %s: cannot open for guestFs\n", imageId);
    ++failures;
    return;
  }
  guestFsIo io;
  guestFsImageBind(&io, &image);
  guestFsAppleDevice device;
  guestFsStatus status = guestFsAppleOpenDevice(&device, &io, imageId, detail, sizeof(detail));
  if (status != guestFsOk)
  {
    printf("FAIL %s: guestFsAppleOpenDevice -> %s (%s)\n", imageId, guestFsStatusText(status),
           detail);
    ++failures;
  }
  else if (device.dosOrder != referenceDos)
  {
    printf("FAIL %s: guestFs order %s, emulator order %s\n", imageId,
           device.dosOrder ? "DOS" : "ProDOS", referenceDos ? "DOS" : "ProDOS");
    ++failures;
  }
  else
  {
    printf("ok   %s: %s order\n", imageId, device.dosOrder ? "DOS" : "ProDOS");
  }
  ++checkedImages;
  imageClose(&image);
}

static void checkDirectory(const char* root, const char* subPath, const char* osId)
{
  char directory[1024];
  snprintf(directory, sizeof(directory), "%s/%s/%s", root, subPath, osId);
  DIR* handle = opendir(directory);
  if (handle == NULL)
  {
    return;
  }
  struct dirent* item;
  while ((item = readdir(handle)) != NULL)
  {
    const char* dot = strrchr(item->d_name, '.');
    if (dot != NULL && (strcasecmp(dot, ".po") == 0 || strcasecmp(dot, ".dsk") == 0 ||
                        strcasecmp(dot, ".do") == 0 || strcasecmp(dot, ".img") == 0))
    {
      checkImage(directory, osId, item->d_name);
    }
  }
  closedir(handle);
}

int main(int argumentCount, char** arguments)
{
  const char* root = argumentCount > 1 ? arguments[1] : ".";
  const char* osIds[] = {"apple2", "ucsd"};
  for (size_t index = 0; index < 2; ++index)
  {
    checkDirectory(root, "sdcard/retro/images", osIds[index]);
    checkDirectory(root, "littlefs", osIds[index]);
  }
  printf("%d image(s) compared with apple2DiskImage, %d failure(s)\n", checkedImages, failures);
  return failures == 0 && checkedImages > 0 ? 0 : 1;
}
