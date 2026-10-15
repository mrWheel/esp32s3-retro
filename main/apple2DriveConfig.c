#include "apple2DriveConfig.h"
#include "storagePath.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const char *systemImagePath = "/littlefs/apple2/system.dsk";
static const char *systemPathPrefix = "/littlefs/apple2/";
static const char *pascalSystemPathPrefix = "/littlefs/ucsd/";
static const char *dataPathPrefix = "/retro/images/apple2/";
static const char *pascalDataPathPrefix = "/retro/images/ucsd/";

typedef apple2DriveConfig driveTable[apple2DriveConfigSlotCount][apple2DriveConfigDevicesPerSlot];

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
  memset(drives, 0, sizeof(apple2DriveConfig) * apple2DriveConfigSlotCount * apple2DriveConfigDevicesPerSlot);
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

apple2DriveConfigResult apple2DriveConfigResolve(const char *configPath, char *drivesPath,
                                                 size_t drivesPathCapacity, char *error, size_t errorCapacity)
{
  if (configPath == NULL || drivesPath == NULL || drivesPathCapacity == 0)
  {
    setError(error, errorCapacity, "Invalid config.cfg arguments");
    return apple2DriveConfigInvalid;
  }

  drivesPath[0] = '\0';
  FILE *file = fopen(configPath, "rb");
  if (file == NULL)
  {
    if (errno == ENOENT)
    {
      setError(error, errorCapacity, "config.cfg is missing");
      return apple2DriveConfigMissing;
    }
    setError(error, errorCapacity, "config.cfg could not be opened");
    return apple2DriveConfigInvalid;
  }

  bool foundDrivesPath = false;
  char line[256];
  char reason[96];
  size_t lineNumber = 0;
  while (fgets(line, sizeof(line), file) != NULL)
  {
    ++lineNumber;
    size_t length = strlen(line);
    reason[0] = '\0';
    if (length == sizeof(line) - 1 && line[length - 1] != '\n' && !feof(file))
    {
      snprintf(reason, sizeof(reason), "line is too long");
    }
    else
    {
      char *content = trim(line);
      if (*content != '\0' && *content != '#')
      {
        char *equals = strchr(content, '=');
        if (equals == NULL || equals == content || strchr(equals + 1, '=') != NULL)
        {
          snprintf(reason, sizeof(reason), "expected drives=/retro/images/<apple2|ucsd>/<file>.cfg");
        }
        else
        {
          *equals = '\0';
          char *key = trim(content);
          char *value = trim(equals + 1);
          size_t valueLength = strlen(value);
          size_t drivesFileNameLength = strlen("drives.cfg");
          if (strcmp(key, "drives") != 0 || valueLength < drivesFileNameLength ||
              strcmp(value + valueLength - drivesFileNameLength, "drives.cfg") != 0 ||
              (!safePath(value, dataPathPrefix) && !safePath(value, pascalDataPathPrefix)))
          {
            snprintf(reason, sizeof(reason),
                     "drives must name a drives.cfg below /retro/images/apple2/ or /retro/images/ucsd/");
          }
          else if (foundDrivesPath)
          {
            snprintf(reason, sizeof(reason), "drives is configured more than once");
          }
          else if (!storageResolveRetroPath(value, drivesPath, drivesPathCapacity))
          {
            snprintf(reason, sizeof(reason), "drives path could not be resolved");
          }
          else
          {
            foundDrivesPath = true;
          }
        }
      }
    }
    if (reason[0] != '\0')
    {
      fclose(file);
      drivesPath[0] = '\0';
      if (error != NULL && errorCapacity > 0)
      {
        snprintf(error, errorCapacity, "config.cfg line %u: %s", (unsigned)lineNumber, reason);
      }
      return apple2DriveConfigInvalid;
    }
  }

  bool readError = ferror(file) != 0;
  fclose(file);
  if (readError)
  {
    drivesPath[0] = '\0';
    setError(error, errorCapacity, "config.cfg could not be read completely");
    return apple2DriveConfigInvalid;
  }
  if (!foundDrivesPath)
  {
    setError(error, errorCapacity, "config.cfg does not select a drives.cfg");
    return apple2DriveConfigInvalid;
  }
  setError(error, errorCapacity, "");
  return apple2DriveConfigLoaded;
}

//-- Parses "SD<slot>.<drive>" or "SP<slot>.<unit>" into zero-based table indexes.
static bool parseDriveName(const char *name, size_t *slotIndex, size_t *driveIndex, bool *smartPort)
{
  if (strlen(name) != 5 || name[3] != '.' ||
      name[2] < '0' + apple2DriveConfigFirstSlot || name[2] > '0' + apple2DriveConfigLastSlot)
  {
    return false;
  }
  if (name[0] == 'S' && name[1] == 'D')
  {
    *smartPort = false;
  }
  else if (name[0] == 'S' && name[1] == 'P')
  {
    *smartPort = true;
  }
  else
  {
    return false;
  }
  size_t maxUnits = *smartPort ? apple2DriveConfigSmartPortUnits : apple2DriveConfigDrivesPerSlot;
  if (name[4] < '1' || name[4] >= '1' + maxUnits)
  {
    return false;
  }
  *slotIndex = (size_t)(name[2] - '0' - apple2DriveConfigFirstSlot);
  *driveIndex = (size_t)(name[4] - '1');
  return true;
}

static bool parseLine(char *line, driveTable drives, bool seen[apple2DriveConfigSlotCount][apple2DriveConfigDevicesPerSlot],
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
    setError(error, errorCapacity, "expected SD<slot>.<drive> or SP<slot>.<unit>=<image>,<RO|RW>,<profile>");
    return false;
  }
  *equals = '\0';
  char *driveName = trim(content);
  char *fields = trim(equals + 1);
  if (strcmp(driveName, "MACHINE") == 0)
  {
    if (strcmp(fields, "II") != 0 && strcmp(fields, "IIE") != 0)
    {
      setError(error, errorCapacity, "MACHINE must be II or IIE");
      return false;
    }
    return true;
  }
  if (strcmp(driveName, "RAMCARD") == 0)
  {
    if (strcmp(fields, "LC16") != 0 && strcmp(fields, "SATURN128") != 0)
    {
      setError(error, errorCapacity, "RAMCARD must be LC16 or SATURN128");
      return false;
    }
    return true;
  }
  size_t slotIndex;
  size_t driveIndex;
  bool smartPort;
  if (!parseDriveName(driveName, &slotIndex, &driveIndex, &smartPort))
  {
    setError(error, errorCapacity, "controller must be SD4.1..SD7.2 or SP4.1..SP7.4");
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
  for (size_t index = 0; index < apple2DriveConfigDevicesPerSlot; ++index)
  {
    if (drives[slotIndex][index].configured && drives[slotIndex][index].smartPort != smartPort)
    {
      setError(error, errorCapacity, "a slot cannot contain both Disk II and SmartPort devices");
      return false;
    }
  }
  bool apple2System = strncmp(imagePath, systemPathPrefix, strlen(systemPathPrefix)) == 0;
  bool pascalSystem = strncmp(imagePath, pascalSystemPathPrefix, strlen(pascalSystemPathPrefix)) == 0;
  bool littlefsImage = apple2System || pascalSystem;
  bool pascalData = strncmp(imagePath, pascalDataPathPrefix, strlen(pascalDataPathPrefix)) == 0;
  bool pascalBootDrive = !smartPort && slotIndex + apple2DriveConfigFirstSlot == 6 && driveIndex == 0;
  if (pascalSystem && !readOnly && !pascalBootDrive)
  {
    setError(error, errorCapacity, "only the SD6.1 UCSD boot image may be writable in LittleFS");
    return false;
  }
  if (!readOnly && strcmp(imagePath, systemImagePath) == 0)
  {
    setError(error, errorCapacity, "system.dsk must be read-only");
    return false;
  }
  if (pascalSystem && !safePath(imagePath, pascalSystemPathPrefix))
  {
    setError(error, errorCapacity, "unsafe UCSD LittleFS image path");
    return false;
  }
  const char *allowedPrefix =
      littlefsImage ? (pascalSystem ? pascalSystemPathPrefix : systemPathPrefix) : (pascalData ? pascalDataPathPrefix : dataPathPrefix);
  if (!safePath(imagePath, allowedPrefix) || strlen(imagePath) >= sizeof(drive->path))
  {
    setError(error, errorCapacity,
             "image path must be /littlefs/apple2/system.dsk, below /littlefs/ucsd/, or below /retro/images/apple2/ or /retro/images/ucsd/");
    return false;
  }
  if (littlefsImage)
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
                                              apple2DriveConfig drives[apple2DriveConfigSlotCount][apple2DriveConfigDevicesPerSlot],
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

  bool seen[apple2DriveConfigSlotCount][apple2DriveConfigDevicesPerSlot] = {{false}};
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

static bool settingHasValue(const char *path, const char *key, const char *expected)
{
  FILE *file = path == NULL ? NULL : fopen(path, "rb");
  if (file == NULL)
  {
    return false;
  }
  bool matches = false;
  char line[256];
  while (fgets(line, sizeof(line), file) != NULL)
  {
    char *content = trim(line);
    char *equals = strchr(content, '=');
    if (*content == '#' || equals == NULL)
    {
      continue;
    }
    *equals = '\0';
    if (strcmp(trim(content), key) == 0)
    {
      matches = strcmp(trim(equals + 1), expected) == 0;
    }
  }
  fclose(file);
  return matches;
}

bool apple2DriveConfigRamCardIsSaturn128(const char *path)
{
  return settingHasValue(path, "RAMCARD", "SATURN128");
}

bool apple2DriveConfigMachineIsIIe(const char *path)
{
  return settingHasValue(path, "MACHINE", "IIE");
}
