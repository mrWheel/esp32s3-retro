#include "apple2DriveConfig.h"
#include "storagePath.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *systemImagePath = "/littlefs/apple2/system.dsk";
static const char *systemPathPrefix = "/littlefs/apple2/";
static const char *dataPathPrefix = "/retro/images/apple2/";

typedef apple2DriveConfig driveTable[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot];

static void setError(char *error, size_t errorCapacity, const char *message)
{
  if (error != NULL && errorCapacity > 0)
  {
    snprintf(error, errorCapacity, "%s", message);
  }
}

//-- There is no default drive: without a drives.cfg line nothing is attached.
static void initializeDefaults(driveTable drives)
{
  memset(drives, 0, sizeof(apple2DriveConfig) * apple2DriveConfigSlotCount * apple2DriveConfigDrivesPerSlot);
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

//-- Parses "SD<slot>.<drive>" or the slot-5 SmartPort form "SP5.<unit>" into zero-based table indexes.
static bool parseDriveName(const char *name, size_t *slotIndex, size_t *driveIndex, bool *smartPort)
{
  if (strlen(name) != 5 || name[3] != '.' ||
      name[2] < '0' + apple2DriveConfigFirstSlot || name[2] > '0' + apple2DriveConfigLastSlot ||
      name[4] < '1' || name[4] >= '1' + apple2DriveConfigDrivesPerSlot)
  {
    return false;
  }
  if (name[0] == 'S' && name[1] == 'D')
  {
    *smartPort = false;
  }
  else if (name[0] == 'S' && name[1] == 'P' && name[2] == '5')
  {
    *smartPort = true;
  }
  else
  {
    return false;
  }
  *slotIndex = (size_t)(name[2] - '0' - apple2DriveConfigFirstSlot);
  *driveIndex = (size_t)(name[4] - '1');
  return true;
}

static bool parseLine(char *line, driveTable drives, bool seen[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot],
                      char *error, size_t errorCapacity)
{
  char *content = trim(line);
  if (*content == '\0' || *content == '#')
  {
    return true;
  }
  char *equals = strchr(content, '=');
  if (equals == NULL || equals == content || strchr(equals + 1, '=') != NULL)
  {
    setError(error, errorCapacity, "expected SD<slot>.<drive> or SP5.<unit>=<image>,<RO|RW>,<profile>");
    return false;
  }
  *equals = '\0';
  char *driveName = trim(content);
  char *fields = trim(equals + 1);
  size_t slotIndex;
  size_t driveIndex;
  bool smartPort;
  if (!parseDriveName(driveName, &slotIndex, &driveIndex, &smartPort))
  {
    setError(error, errorCapacity, "controller must be SD4.1..SD7.2 or SP5.1..SP5.2");
    return false;
  }
  if (seen[slotIndex][driveIndex])
  {
    setError(error, errorCapacity, "drive configured twice");
    return false;
  }

  char *firstComma = strchr(fields, ',');
  if (firstComma == NULL)
  {
    setError(error, errorCapacity, "expected <image>,<RO|RW>,<profile>");
    return false;
  }
  *firstComma = '\0';
  char *mode = trim(firstComma + 1);
  char *secondComma = strchr(mode, ',');
  if (secondComma == NULL)
  {
    setError(error, errorCapacity, "expected <image>,<RO|RW>,<profile>");
    return false;
  }
  *secondComma = '\0';
  char *profileName = trim(secondComma + 1);
  char *imagePath = trim(fields);
  mode = trim(mode);
  if (*imagePath == '\0' || *profileName == '\0' || strchr(profileName, ',') != NULL)
  {
    setError(error, errorCapacity, "expected <image>,<RO|RW>,<profile>");
    return false;
  }

  apple2DiskImageProfile profile;
  if (strcmp(profileName, "APPLE2_140K") == 0)
  {
    profile = apple2DiskImageProfile140k;
  }
  else if (strcmp(profileName, "APPLE2_640K") == 0)
  {
    profile = apple2DiskImageProfile640k;
  }
  else if (strcmp(profileName, "APPLE2_800K") == 0)
  {
    profile = apple2DiskImageProfile800k;
  }
  else
  {
    setError(error, errorCapacity, "profile must be APPLE2_140K, APPLE2_640K or APPLE2_800K");
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
    setError(error, errorCapacity, "mode must be RO or RW");
    return false;
  }

  apple2DriveConfig *drive = &drives[slotIndex][driveIndex];
  if (slotIndex == 5 - apple2DriveConfigFirstSlot)
  {
    for (size_t index = 0; index < apple2DriveConfigDrivesPerSlot; ++index)
    {
      if (drives[slotIndex][index].configured && drives[slotIndex][index].smartPort != smartPort)
      {
        setError(error, errorCapacity, "slot 5 cannot contain both Disk II and SmartPort devices");
        return false;
      }
    }
  }
  bool system = strncmp(imagePath, systemPathPrefix, strlen(systemPathPrefix)) == 0;
  if (!readOnly && strcmp(imagePath, systemImagePath) == 0)
  {
    setError(error, errorCapacity, "system.dsk must be read-only");
    return false;
  }
  if (!safePath(imagePath, system ? systemPathPrefix : dataPathPrefix) || strlen(imagePath) >= sizeof(drive->path))
  {
    setError(error, errorCapacity, "image path must be below /littlefs/apple2/ or /retro/images/apple2/");
    return false;
  }
  if (system)
  {
    snprintf(drive->path, sizeof(drive->path), "%s", imagePath);
  }
  else if (!storageResolveRetroPath(imagePath, drive->path, sizeof(drive->path)))
  {
    setError(error, errorCapacity, "image path could not be resolved");
    return false;
  }
  drive->profile = profile;
  drive->readOnly = readOnly;
  drive->smartPort = smartPort;
  drive->configured = true;
  seen[slotIndex][driveIndex] = true;
  return true;
}

apple2DriveConfigResult apple2DriveConfigLoad(const char *path,
                                              apple2DriveConfig drives[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot],
                                              char *error, size_t errorCapacity)
{
  if (path == NULL || drives == NULL)
  {
    setError(error, errorCapacity, "Invalid drives.cfg arguments");
    return apple2DriveConfigInvalid;
  }

  initializeDefaults(drives);
  FILE *file = fopen(path, "rb");
  if (file == NULL)
  {
    if (errno == ENOENT)
    {
      setError(error, errorCapacity, "drives.cfg is missing; no Apple II drives are attached");
      return apple2DriveConfigMissing;
    }
    setError(error, errorCapacity, "drives.cfg could not be opened");
    return apple2DriveConfigInvalid;
  }

  bool seen[apple2DriveConfigSlotCount][apple2DriveConfigDrivesPerSlot] = {{false}};
  char line[256];
  char reason[96];
  size_t lineNumber = 0;
  while (fgets(line, sizeof(line), file) != NULL)
  {
    ++lineNumber;
    size_t length = strlen(line);
    reason[0] = '\0';
    if ((length == sizeof(line) - 1 && line[length - 1] != '\n' && !feof(file)))
    {
      snprintf(reason, sizeof(reason), "line is too long");
    }
    else if (!parseLine(line, drives, seen, reason, sizeof(reason)))
    {
      if (reason[0] == '\0')
      {
        snprintf(reason, sizeof(reason), "invalid line");
      }
    }
    if (reason[0] != '\0')
    {
      fclose(file);
      initializeDefaults(drives);
      if (error != NULL && errorCapacity > 0)
      {
        snprintf(error, errorCapacity, "drives.cfg line %u: %s", (unsigned)lineNumber, reason);
      }
      return apple2DriveConfigInvalid;
    }
  }

  bool readError = ferror(file) != 0;
  fclose(file);
  if (readError)
  {
    initializeDefaults(drives);
    setError(error, errorCapacity, "drives.cfg could not be read completely");
    return apple2DriveConfigInvalid;
  }
  setError(error, errorCapacity, "");
  return apple2DriveConfigLoaded;
}
