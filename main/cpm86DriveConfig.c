#include "cpm86DriveConfig.h"
#include "storagePath.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *systemImagePath = "/littlefs/cpm86/system.dsk";
static const char *systemPathPrefix = "/littlefs/cpm86/";
static const char *dataPathPrefix = "/retro/images/cpm86/";

static void setError(char *error, size_t errorCapacity, const char *message)
{
  if (error != NULL && errorCapacity > 0)
  {
    snprintf(error, errorCapacity, "%s", message);
  }
}

static void initializeDefaults(cpm86DriveConfig drives[cpm86DiskDriveCount])
{
  memset(drives, 0, sizeof(cpm86DriveConfig) * cpm86DiskDriveCount);
  snprintf(drives[0].path, sizeof(drives[0].path), "%s", systemImagePath);
  drives[0].profile = cpm86DiskProfileSystem;
  drives[0].readOnly = true;
  drives[0].configured = true;
}

static char *trim(char *text)
{
  while (*text == ' ' || *text == '\t')
  {
    ++text;
  }
  size_t length = strlen(text);
  while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r' ||
                        text[length - 1] == '\n'))
  {
    text[--length] = '\0';
  }
  return text;
}

static bool safePath(const char *path, const char *prefix)
{
  size_t pathLength = strlen(path);
  size_t prefixLength = strlen(prefix);
  if (pathLength <= prefixLength || strncmp(path, prefix, prefixLength) != 0 || path[pathLength - 1] == '/' ||
      strstr(path, "//") != NULL || strstr(path, "/./") != NULL || strstr(path, "/../") != NULL ||
      (pathLength >= 2 && strcmp(path + pathLength - 2, "/.") == 0) ||
      (pathLength >= 3 && strcmp(path + pathLength - 3, "/..") == 0) || strchr(path, '\\') != NULL ||
      strchr(path, ',') != NULL)
  {
    return false;
  }
  for (const unsigned char *character = (const unsigned char *)path; *character != '\0'; ++character)
  {
    if (*character < 0x20 || *character == 0x7f)
    {
      return false;
    }
  }
  return true;
}

static bool parseLine(char *line, cpm86DriveConfig drives[cpm86DiskDriveCount],
                      bool seen[cpm86DiskDriveCount])
{
  char *content = trim(line);
  if (*content == '\0' || *content == '#')
  {
    return true;
  }
  char *equals = strchr(content, '=');
  if (equals == NULL || equals == content || strchr(equals + 1, '=') != NULL)
  {
    return false;
  }
  *equals = '\0';
  char *driveName = trim(content);
  char *fields = trim(equals + 1);
  if (strlen(driveName) != 1 || driveName[0] < 'A' || driveName[0] >= 'A' + cpm86DiskDriveCount)
  {
    return false;
  }
  uint8_t drive = (uint8_t)(driveName[0] - 'A');
  if (seen[drive])
  {
    return false;
  }

  char *firstComma = strchr(fields, ',');
  if (firstComma == NULL)
  {
    return false;
  }
  *firstComma = '\0';
  char *mode = trim(firstComma + 1);
  char *secondComma = strchr(mode, ',');
  if (secondComma == NULL)
  {
    return false;
  }
  *secondComma = '\0';
  char *profileName = trim(secondComma + 1);
  char *imagePath = trim(fields);
  mode = trim(mode);
  if (*imagePath == '\0' || *profileName == '\0' || strchr(profileName, ',') != NULL)
  {
    return false;
  }

  cpm86DiskProfile profile;
  if (strcmp(profileName, "RETRO86_SYSTEM") == 0 || strcmp(profileName, "RETRO86_SYSTEM_V1") == 0)
  {
    profile = cpm86DiskProfileSystem;
  }
  else if (strcmp(profileName, "RETRO86_DATA_V1") == 0)
  {
    profile = cpm86DiskProfileData;
  }
  else if (strcmp(profileName, "RETRO86_DATA_LARGE_V1") == 0)
  {
    profile = cpm86DiskProfileDataLarge;
  }
  else if (strcmp(profileName, "RETRO86_DATA_BIG_V1") == 0)
  {
    profile = cpm86DiskProfileDataBig;
  }
  else
  {
    return false;
  }

  bool readOnly;
  if (strcmp(mode, "RO") == 0)
  {
    readOnly = true;
  }
  else if (strcmp(mode, "RW") == 0)
  {
    readOnly = false;
  }
  else
  {
    return false;
  }

  const char *requiredPrefix = drive == 0 ? systemPathPrefix : dataPathPrefix;
  if ((drive == 0 && (profile != cpm86DiskProfileSystem || !readOnly ||
                      strcmp(imagePath, systemImagePath) != 0)) ||
      (drive != 0 && profile == cpm86DiskProfileSystem) || !safePath(imagePath, requiredPrefix) ||
      strlen(imagePath) >= sizeof(drives[drive].path))
  {
    return false;
  }

  if (drive == 0)
  {
    snprintf(drives[drive].path, sizeof(drives[drive].path), "%s", imagePath);
  }
  else
  {
    if (!storageResolveRetroPath(imagePath, drives[drive].path, sizeof(drives[drive].path)))
    {
      return false;
    }
  }
  drives[drive].profile = profile;
  drives[drive].readOnly = readOnly;
  drives[drive].configured = true;
  seen[drive] = true;
  return true;
}

cpm86DriveConfigResult cpm86DriveConfigLoad(const char *path,
                                            cpm86DriveConfig drives[cpm86DiskDriveCount],
                                            char *error, size_t errorCapacity)
{
  if (path == NULL || drives == NULL)
  {
    setError(error, errorCapacity, "Invalid drives.cfg arguments");
    return cpm86DriveConfigInvalid;
  }

  initializeDefaults(drives);
  FILE *file = fopen(path, "rb");
  if (file == NULL)
  {
    if (errno == ENOENT)
    {
      setError(error, errorCapacity, "drives.cfg is missing; using the built-in A: system image only");
      return cpm86DriveConfigMissing;
    }
    setError(error, errorCapacity, "drives.cfg could not be opened");
    return cpm86DriveConfigInvalid;
  }

  bool seen[cpm86DiskDriveCount] = {false};
  char line[256];
  size_t lineNumber = 0;
  while (fgets(line, sizeof(line), file) != NULL)
  {
    ++lineNumber;
    size_t length = strlen(line);
    if (length == sizeof(line) - 1 && line[length - 1] != '\n' && !feof(file))
    {
      fclose(file);
      initializeDefaults(drives);
      if (error != NULL && errorCapacity > 0)
      {
        snprintf(error, errorCapacity, "drives.cfg line %u is too long", (unsigned)lineNumber);
      }
      return cpm86DriveConfigInvalid;
    }
    if (!parseLine(line, drives, seen))
    {
      fclose(file);
      initializeDefaults(drives);
      if (error != NULL && errorCapacity > 0)
      {
        snprintf(error, errorCapacity, "invalid drives.cfg line %u", (unsigned)lineNumber);
      }
      return cpm86DriveConfigInvalid;
    }
  }

  bool readError = ferror(file) != 0;
  fclose(file);
  if (readError)
  {
    initializeDefaults(drives);
    setError(error, errorCapacity, "drives.cfg could not be read completely");
    return cpm86DriveConfigInvalid;
  }
  setError(error, errorCapacity, "");
  return cpm86DriveConfigLoaded;
}

bool cpm86DriveConfigSystemProfileFromSize(uint64_t imageSize, cpm86DiskProfile *profile)
{
  if (profile == NULL)
  {
    return false;
  }
  if (imageSize == cpm86SystemDiskSize)
  {
    *profile = cpm86DiskProfileSystem;
    return true;
  }
  if (imageSize == cpm86LargeDiskSize)
  {
    *profile = cpm86DiskProfileSystemLarge;
    return true;
  }
  return false;
}

bool cpm86DriveConfigResolveSystemProfile(cpm86DriveConfig *drive, uint64_t imageSize, char *error,
                                          size_t errorCapacity)
{
  cpm86DiskProfile detected;
  if (drive == NULL || !cpm86DriveConfigSystemProfileFromSize(imageSize, &detected))
  {
    if (error != NULL && errorCapacity > 0)
    {
      snprintf(error, errorCapacity, "size %llu matches no system profile (small=%llu, large=%llu)",
               (unsigned long long)imageSize, (unsigned long long)cpm86SystemDiskSize,
               (unsigned long long)cpm86LargeDiskSize);
    }
    return false;
  }
  drive->profile = detected;
  setError(error, errorCapacity, "");
  return true;
}
