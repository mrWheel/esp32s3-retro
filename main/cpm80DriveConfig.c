#include "cpm80DriveConfig.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *systemImagePath = "/littlefs/cpm80/system.dsk";
static const char *systemPathPrefix = "/littlefs/cpm80/";
static const char *largePathPrefix = "/retro/images/cpm80/";
static const char *largeVfsPrefix = "/microSD";

static void setError(char *error, size_t errorCapacity, const char *message)
{
  if (error != NULL && errorCapacity > 0)
  {
    snprintf(error, errorCapacity, "%s", message);
  }
}

static void initializeDefaults(cpm80DriveConfig drives[cpm80DiskDriveCount])
{
  memset(drives, 0, sizeof(cpm80DriveConfig) * cpm80DiskDriveCount);
  snprintf(drives[0].path, sizeof(drives[0].path), "%s", systemImagePath);
  drives[0].profile = cpm80DiskProfileSystem;
  drives[0].readOnly = true;
  drives[0].configured = true;
  drives[0].profileFromSize = true;
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
  size_t prefixLength = strlen(prefix);
  if (strncmp(path, prefix, prefixLength) != 0 || path[prefixLength] == '\0' || path[strlen(path) - 1] == '/' ||
      strstr(path, "//") != NULL || strstr(path, "/./") != NULL || strstr(path, "/../") != NULL ||
      strcmp(path + strlen(path) - 2, "/.") == 0 || strcmp(path + strlen(path) - 3, "/..") == 0 ||
      strchr(path, '\\') != NULL)
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

static bool parseLine(char *line, cpm80DriveConfig drives[cpm80DiskDriveCount], bool seen[cpm80DiskDriveCount])
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
  if (strlen(driveName) != 1 || driveName[0] < 'A' || driveName[0] >= 'A' + cpm80DiskDriveCount)
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
  if (strchr(profileName, ',') != NULL || *imagePath == '\0')
  {
    return false;
  }

  cpm80DiskProfile profile;
  if (strcmp(profileName, "SYSTEM") == 0)
  {
    profile = cpm80DiskProfileSystem;
  }
  else if (strcmp(profileName, "LARGE") == 0)
  {
    profile = cpm80DiskProfileLarge;
  }
  else if (strcmp(profileName, "BIG") == 0)
  {
    profile = cpm80DiskProfileBig;
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

  //-- A: is the LittleFS system image (SYSTEM or LARGE, never BIG); B: to F: are SD images (SYSTEM = 256256 bytes, LARGE = 512512 bytes or BIG = 8421376 bytes).
  const char *requiredPrefix = drive == 0 ? systemPathPrefix : largePathPrefix;
  size_t storedPathLength = strlen(imagePath) + (drive != 0 ? strlen(largeVfsPrefix) : 0);
  if ((drive == 0 && (profile == cpm80DiskProfileBig || !readOnly)) || !safePath(imagePath, requiredPrefix) ||
      storedPathLength >= sizeof(drives[drive].path))
  {
    return false;
  }

  if (drive != 0)
  {
    snprintf(drives[drive].path, sizeof(drives[drive].path), "%s%s", largeVfsPrefix, imagePath);
  }
  else
  {
    snprintf(drives[drive].path, sizeof(drives[drive].path), "%s", imagePath);
  }
  drives[drive].profile = profile;
  drives[drive].readOnly = readOnly;
  drives[drive].configured = true;
  drives[drive].profileFromSize = drive == 0 && profile == cpm80DiskProfileSystem;
  seen[drive] = true;
  return true;
}

cpm80DriveConfigResult cpm80DriveConfigLoad(const char *path, cpm80DriveConfig drives[cpm80DiskDriveCount], char *error,
                                        size_t errorCapacity)
{
  if (path == NULL || drives == NULL)
  {
    setError(error, errorCapacity, "Invalid drives.cfg arguments");
    return cpm80DriveConfigInvalid;
  }

  initializeDefaults(drives);
  FILE *file = fopen(path, "rb");
  if (file == NULL)
  {
    if (errno == ENOENT)
    {
      setError(error, errorCapacity, "drives.cfg is missing; using the built-in A: system image only");
      return cpm80DriveConfigMissing;
    }
    setError(error, errorCapacity, "drives.cfg could not be opened");
    return cpm80DriveConfigInvalid;
  }

  bool seen[cpm80DiskDriveCount] = {false};
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
      return cpm80DriveConfigInvalid;
    }
    if (!parseLine(line, drives, seen))
    {
      fclose(file);
      initializeDefaults(drives);
      if (error != NULL && errorCapacity > 0)
      {
        snprintf(error, errorCapacity, "invalid drives.cfg line %u", (unsigned)lineNumber);
      }
      return cpm80DriveConfigInvalid;
    }
  }

  bool readError = ferror(file) != 0;
  fclose(file);
  if (readError)
  {
    initializeDefaults(drives);
    setError(error, errorCapacity, "drives.cfg could not be read completely");
    return cpm80DriveConfigInvalid;
  }
  setError(error, errorCapacity, "");
  return cpm80DriveConfigLoaded;
}

bool cpm80DriveConfigSystemProfileFromSize(uint64_t imageSize, cpm80DiskProfile *profile)
{
  if (profile == NULL)
  {
    return false;
  }
  if (imageSize == cpm80DiskProfileImageSize(cpm80DiskProfileSystem))
  {
    *profile = cpm80DiskProfileSystem;
    return true;
  }
  if (imageSize == cpm80DiskProfileImageSize(cpm80DiskProfileLarge))
  {
    *profile = cpm80DiskProfileLarge;
    return true;
  }
  return false;
}

bool cpm80DriveConfigResolveSystemProfile(cpm80DriveConfig *drive, uint64_t imageSize, char *error,
                                          size_t errorCapacity)
{
  cpm80DiskProfile detected;
  if (drive == NULL || !cpm80DriveConfigSystemProfileFromSize(imageSize, &detected))
  {
    if (error != NULL && errorCapacity > 0)
    {
      snprintf(error, errorCapacity, "size %llu matches no system profile (SYSTEM=%llu, LARGE=%llu)",
               (unsigned long long)imageSize, (unsigned long long)cpm80DiskProfileImageSize(cpm80DiskProfileSystem),
               (unsigned long long)cpm80DiskProfileImageSize(cpm80DiskProfileLarge));
    }
    return false;
  }
  if (!drive->profileFromSize && drive->profile != detected)
  {
    if (error != NULL && errorCapacity > 0)
    {
      snprintf(error, errorCapacity, "drives.cfg says %s but the image is %s (size %llu)",
               cpm80DiskProfileName(drive->profile), cpm80DiskProfileName(detected), (unsigned long long)imageSize);
    }
    return false;
  }
  drive->profile = detected;
  setError(error, errorCapacity, "");
  return true;
}
